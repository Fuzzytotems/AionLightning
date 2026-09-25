// jlang/src/netty_channel.cpp - org.jboss.netty.channel core (events, pipeline, futures,
// SimpleChannel*Handler, Channels, AbstractChannel, configs), channel groups, bootstrap and the
// frame / one-to-one codecs, ported from Netty 3.2.0.BETA1.
#include "netty_internal.h"

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

#include <cerrno>
#include <chrono>
#include <cstring>

namespace jlang::netty {

using ::jlang::InetSocketAddress;
using ::jlang::Object;
using ::jlang::String;
using ::jlang::Throwable;

// =======================================================================================
// detail helpers
// =======================================================================================
namespace detail {

Object* boxBool(bool v) { return v ? ::jlang::Boolean::TRUE : ::jlang::Boolean::FALSE; }
Object* boxInt(int32_t v) { return ::jlang::Integer::valueOf(v); }

bool isTrue(Object* v) {
    auto* b = dynamic_cast<::jlang::Boolean*>(v);
    return b != nullptr && b->booleanValue();
}
bool isFalse(Object* v) {
    auto* b = dynamic_cast<::jlang::Boolean*>(v);
    return b != nullptr && !b->booleanValue();
}

// Netty ConversionUtil
int32_t toInt(Object* v) {
    if (auto* n = dynamic_cast<::jlang::Number*>(v)) return n->intValue();
    return ::jlang::Integer::parseInt(::jlang::str(v));
}
bool toBoolean(Object* v) {
    if (auto* b = dynamic_cast<::jlang::Boolean*>(v)) return b->booleanValue();
    if (auto* n = dynamic_cast<::jlang::Number*>(v)) return n->intValue() != 0;
    String s = ::jlang::str(v);
    if (s.length() == 0) return false;
    try {
        return ::jlang::Integer::parseInt(s) != 0;
    } catch (::jlang::NumberFormatException&) {
        s = s.toUpperCase();
        return s.startsWith("Y") || s.startsWith("T");
    }
}

Throwable* copyOf(const Throwable& t) { return t.copyThrowable(); }
Throwable* copyOf(const std::exception& e) { return new ::jlang::RuntimeException(String(e.what())); }

namespace {
thread_local bool tlInIoThread = false;
}
bool inIoThread() { return tlInIoThread; }
void setInIoThread(bool v) { tlInIoThread = v; }

int64_t nanoTime() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// ---------------------------------------------------------------------------------------
// The handler context of DefaultChannelPipeline (DefaultChannelPipeline.DefaultChannelHandlerContext).
class PipelineContext final : public virtual ChannelHandlerContext {
public:
    PipelineContext(DefaultChannelPipeline* pipeline, PipelineContext* prev, PipelineContext* next,
                    const String& name, ChannelHandler* handler)
        : pipeline_(pipeline), name_(name), handler_(handler) {
        if (name.isNull()) throw ::jlang::NullPointerException("name");
        if (handler == nullptr) throw ::jlang::NullPointerException("handler");
        up_ = dynamic_cast<ChannelUpstreamHandler*>(handler);
        down_ = dynamic_cast<ChannelDownstreamHandler*>(handler);
        if (up_ == nullptr && down_ == nullptr) {
            throw ::jlang::IllegalArgumentException(
                "handler must be either org.jboss.netty.channel.ChannelUpstreamHandler or "
                "org.jboss.netty.channel.ChannelDownstreamHandler.");
        }
        this->prev.store(prev);
        this->next.store(next);
    }
    Channel* getChannel() override { return pipeline_->getChannel(); }
    ChannelPipeline* getPipeline() override { return pipeline_; }
    String getName() override { return name_; }
    ChannelHandler* getHandler() override { return handler_; }
    bool canHandleUpstream() override { return up_ != nullptr; }
    bool canHandleDownstream() override { return down_ != nullptr; }
    Object* getAttachment() override { return attachment_.load(); }
    void setAttachment(Object* attachment) override { attachment_.store(attachment); }
    void sendDownstream(ChannelEvent* e) override {
        PipelineContext* prevCtx = pipeline_->getActualDownstreamContext(prev.load());
        if (prevCtx == nullptr) {
            try {
                pipeline_->getSink()->eventSunk(pipeline_, e);
            } catch (Throwable& t) {
                pipeline_->notifyHandlerException(e, copyOf(t));
            } catch (std::exception& ex) {
                pipeline_->notifyHandlerException(e, copyOf(ex));
            }
        } else {
            pipeline_->sendDownstream(prevCtx, e);
        }
    }
    void sendUpstream(ChannelEvent* e) override {
        PipelineContext* nextCtx = pipeline_->getActualUpstreamContext(next.load());
        if (nextCtx != nullptr) pipeline_->sendUpstream(nextCtx, e);
    }

    std::atomic<PipelineContext*> next{nullptr};
    std::atomic<PipelineContext*> prev{nullptr};
    ChannelUpstreamHandler* up_ = nullptr;
    ChannelDownstreamHandler* down_ = nullptr;

private:
    DefaultChannelPipeline* pipeline_;
    String name_;
    ChannelHandler* handler_;
    std::atomic<Object*> attachment_{nullptr};
};

// DefaultChannelPipeline.DiscardingChannelSink
class DiscardingChannelSink final : public virtual ChannelSink {
public:
    void eventSunk(ChannelPipeline*, ChannelEvent* e) override {
        logWarn("org.jboss.netty.channel.DefaultChannelPipeline", ::jlang::str("Not attached yet; discarding: ", e));
    }
    void exceptionCaught(ChannelPipeline*, ChannelEvent*, ChannelPipelineException& cause) override {
        cause.rethrow();
    }
};

ChannelSink* discardingSink() {
    static ChannelSink* const sink = new DiscardingChannelSink();
    return sink;
}

}  // namespace detail

using detail::PipelineContext;

// =======================================================================================
// ChannelState
// =======================================================================================
String ChannelState::name() const {
    switch (v_) {
    case Value::OPEN: return String("OPEN");
    case Value::BOUND: return String("BOUND");
    case Value::CONNECTED: return String("CONNECTED");
    case Value::INTEREST_OPS: return String("INTEREST_OPS");
    default: return String("null");
    }
}

// =======================================================================================
// Events
// =======================================================================================

UpstreamChannelStateEvent::UpstreamChannelStateEvent(Channel* channel, ChannelState state, Object* value)
    : channel_(channel), state_(state), value_(value) {
    if (channel == nullptr) throw ::jlang::NullPointerException("channel");
    if (state == nullptr) throw ::jlang::NullPointerException("state");
}

ChannelFuture* UpstreamChannelStateEvent::getFuture() { return Channels::succeededFuture(channel_); }

String UpstreamChannelStateEvent::toString() {
    String buf = channel_->toString();
    switch (state_) {
    case ChannelState::OPEN: buf += detail::isTrue(value_) ? " OPEN" : " CLOSED"; break;
    case ChannelState::BOUND:
        if (value_ != nullptr) buf += ::jlang::str(" BOUND: ", value_);
        else buf += " UNBOUND";
        break;
    case ChannelState::CONNECTED:
        if (value_ != nullptr) buf += ::jlang::str(" CONNECTED: ", value_);
        else buf += " DISCONNECTED";
        break;
    case ChannelState::INTEREST_OPS: buf += " INTEREST_CHANGED"; break;
    default: buf += ::jlang::str(state_.name(), ": ", value_);
    }
    return buf;
}

DownstreamChannelStateEvent::DownstreamChannelStateEvent(Channel* channel, ChannelFuture* future, ChannelState state,
                                                         Object* value)
    : channel_(channel), future_(future), state_(state), value_(value) {
    if (channel == nullptr) throw ::jlang::NullPointerException("channel");
    if (future == nullptr) throw ::jlang::NullPointerException("future");
    if (state == nullptr) throw ::jlang::NullPointerException("state");
}

String DownstreamChannelStateEvent::toString() {
    String buf = channel_->toString();
    switch (state_) {
    case ChannelState::OPEN: buf += detail::isTrue(value_) ? " OPEN" : " CLOSE"; break;
    case ChannelState::BOUND:
        if (value_ != nullptr) buf += ::jlang::str(" BIND: ", value_);
        else buf += " UNBIND";
        break;
    case ChannelState::CONNECTED:
        if (value_ != nullptr) buf += ::jlang::str(" CONNECT: ", value_);
        else buf += " DISCONNECT";
        break;
    case ChannelState::INTEREST_OPS: buf += ::jlang::str(" CHANGE_INTEREST: ", value_); break;
    default: buf += ::jlang::str(" ", state_.name(), ": ", value_);
    }
    return buf;
}

UpstreamMessageEvent::UpstreamMessageEvent(Channel* channel, Object* message, InetSocketAddress* remoteAddress)
    : channel_(channel), message_(message) {
    if (channel == nullptr) throw ::jlang::NullPointerException("channel");
    if (message == nullptr) throw ::jlang::NullPointerException("message");
    remoteAddress_ = remoteAddress != nullptr ? remoteAddress : channel->getRemoteAddress();
}

ChannelFuture* UpstreamMessageEvent::getFuture() { return Channels::succeededFuture(channel_); }
InetSocketAddress* UpstreamMessageEvent::getRemoteAddress() { return remoteAddress_; }

String UpstreamMessageEvent::toString() {
    if (getRemoteAddress() == channel_->getRemoteAddress()) {
        return ::jlang::str(channel_->toString(), " RECEIVED: ", message_);
    }
    return ::jlang::str(channel_->toString(), " RECEIVED: ", message_, " from ", getRemoteAddress());
}

DownstreamMessageEvent::DownstreamMessageEvent(Channel* channel, ChannelFuture* future, Object* message,
                                               InetSocketAddress* remoteAddress)
    : channel_(channel), future_(future), message_(message) {
    if (channel == nullptr) throw ::jlang::NullPointerException("channel");
    if (future == nullptr) throw ::jlang::NullPointerException("future");
    if (message == nullptr) throw ::jlang::NullPointerException("message");
    remoteAddress_ = remoteAddress != nullptr ? remoteAddress : channel->getRemoteAddress();
}

InetSocketAddress* DownstreamMessageEvent::getRemoteAddress() { return remoteAddress_; }

String DownstreamMessageEvent::toString() {
    if (getRemoteAddress() == channel_->getRemoteAddress()) {
        return ::jlang::str(channel_->toString(), " WRITE: ", message_);
    }
    return ::jlang::str(channel_->toString(), " WRITE: ", message_, " to ", getRemoteAddress());
}

DefaultExceptionEvent::DefaultExceptionEvent(Channel* channel, Throwable* cause) : channel_(channel), cause_(cause) {
    if (channel == nullptr) throw ::jlang::NullPointerException("channel");
    if (cause == nullptr) throw ::jlang::NullPointerException("cause");
}

ChannelFuture* DefaultExceptionEvent::getFuture() { return Channels::succeededFuture(channel_); }

String DefaultExceptionEvent::toString() { return ::jlang::str(channel_->toString(), " EXCEPTION: ", cause_); }

DefaultWriteCompletionEvent::DefaultWriteCompletionEvent(Channel* channel, int64_t writtenAmount)
    : channel_(channel), writtenAmount_(writtenAmount) {
    if (channel == nullptr) throw ::jlang::NullPointerException("channel");
    if (writtenAmount <= 0) {
        throw ::jlang::IllegalArgumentException(
            ::jlang::str("writtenAmount must be a positive integer: ", writtenAmount));
    }
}

ChannelFuture* DefaultWriteCompletionEvent::getFuture() { return Channels::succeededFuture(channel_); }

String DefaultWriteCompletionEvent::toString() {
    return ::jlang::str(channel_->toString(), " WRITTEN_AMOUNT: ", writtenAmount_);
}

DefaultChildChannelStateEvent::DefaultChildChannelStateEvent(Channel* parentChannel, Channel* childChannel)
    : parentChannel_(parentChannel), childChannel_(childChannel) {
    if (parentChannel == nullptr) throw ::jlang::NullPointerException("parentChannel");
    if (childChannel == nullptr) throw ::jlang::NullPointerException("childChannel");
}

ChannelFuture* DefaultChildChannelStateEvent::getFuture() { return Channels::succeededFuture(parentChannel_); }

String DefaultChildChannelStateEvent::toString() {
    return ::jlang::str(parentChannel_->toString(), childChannel_->isOpen() ? " CHILD_OPEN: " : " CHILD_CLOSED: ",
                        childChannel_->getId());
}

// =======================================================================================
// ChannelSink (AbstractChannelSink.exceptionCaught)
// =======================================================================================
void ChannelSink::exceptionCaught(ChannelPipeline*, ChannelEvent* event, ChannelPipelineException& cause) {
    Throwable* actualCause = cause.getCause();
    if (actualCause == nullptr) actualCause = cause.copyThrowable();
    Channels::fireExceptionCaught(event->getChannel(), actualCause);
}

// =======================================================================================
// DefaultChannelPipeline
// =======================================================================================

ChannelSink* DefaultChannelPipeline::getSink() {
    ChannelSink* s = sink_.load();
    return s == nullptr ? detail::discardingSink() : s;
}

void DefaultChannelPipeline::attach(Channel* channel, ChannelSink* sink) {
    if (channel == nullptr) throw ::jlang::NullPointerException("channel");
    if (sink == nullptr) throw ::jlang::NullPointerException("sink");
    if (channel_.load() != nullptr || sink_.load() != nullptr) throw ::jlang::IllegalStateException("attached already");
    channel_.store(channel);
    sink_.store(sink);
}

void DefaultChannelPipeline::addFirst(const String& name, ChannelHandler* handler) {
    std::lock_guard<std::recursive_mutex> g(lock_);
    if (name2ctx_.empty()) {
        init(name, handler);
        return;
    }
    checkDuplicateName(name);
    PipelineContext* oldHead = head_.load();
    auto* newHead = new PipelineContext(this, nullptr, oldHead, name, handler);
    callBeforeAdd(newHead);
    oldHead->prev.store(newHead);
    head_.store(newHead);
    name2ctx_[name] = newHead;
    callAfterAdd(newHead);
}

void DefaultChannelPipeline::addLast(const String& name, ChannelHandler* handler) {
    std::lock_guard<std::recursive_mutex> g(lock_);
    if (name2ctx_.empty()) {
        init(name, handler);
        return;
    }
    checkDuplicateName(name);
    PipelineContext* oldTail = tail_.load();
    auto* newTail = new PipelineContext(this, oldTail, nullptr, name, handler);
    callBeforeAdd(newTail);
    oldTail->next.store(newTail);
    tail_.store(newTail);
    name2ctx_[name] = newTail;
    callAfterAdd(newTail);
}

void DefaultChannelPipeline::addBefore(const String& baseName, const String& name, ChannelHandler* handler) {
    std::lock_guard<std::recursive_mutex> g(lock_);
    PipelineContext* ctx = contextOrDie(baseName);
    if (ctx == head_.load()) {
        addFirst(name, handler);
        return;
    }
    checkDuplicateName(name);
    auto* newCtx = new PipelineContext(this, ctx->prev.load(), ctx, name, handler);
    callBeforeAdd(newCtx);
    ctx->prev.load()->next.store(newCtx);
    ctx->prev.store(newCtx);
    name2ctx_[name] = newCtx;
    callAfterAdd(newCtx);
}

void DefaultChannelPipeline::addAfter(const String& baseName, const String& name, ChannelHandler* handler) {
    std::lock_guard<std::recursive_mutex> g(lock_);
    PipelineContext* ctx = contextOrDie(baseName);
    if (ctx == tail_.load()) {
        addLast(name, handler);
        return;
    }
    checkDuplicateName(name);
    auto* newCtx = new PipelineContext(this, ctx, ctx->next.load(), name, handler);
    callBeforeAdd(newCtx);
    ctx->next.load()->prev.store(newCtx);
    ctx->next.store(newCtx);
    name2ctx_[name] = newCtx;
    callAfterAdd(newCtx);
}

void DefaultChannelPipeline::remove(ChannelHandler* handler) {
    std::lock_guard<std::recursive_mutex> g(lock_);
    removeContext(contextOrDie(handler));
}

ChannelHandler* DefaultChannelPipeline::remove(const String& name) {
    std::lock_guard<std::recursive_mutex> g(lock_);
    return removeContext(contextOrDie(name))->getHandler();
}

PipelineContext* DefaultChannelPipeline::removeContext(PipelineContext* ctx) {
    if (head_.load() == tail_.load()) {
        head_.store(nullptr);
        tail_.store(nullptr);
        name2ctx_.clear();
    } else if (ctx == head_.load()) {
        removeFirst();
    } else if (ctx == tail_.load()) {
        removeLast();
    } else {
        callBeforeRemove(ctx);
        PipelineContext* prev = ctx->prev.load();
        PipelineContext* next = ctx->next.load();
        prev->next.store(next);
        next->prev.store(prev);
        name2ctx_.erase(ctx->getName());
        callAfterRemove(ctx);
    }
    return ctx;
}

ChannelHandler* DefaultChannelPipeline::removeFirst() {
    std::lock_guard<std::recursive_mutex> g(lock_);
    if (name2ctx_.empty()) throw ::jlang::NoSuchElementException();
    PipelineContext* oldHead = head_.load();
    if (oldHead == nullptr) throw ::jlang::NoSuchElementException();
    callBeforeRemove(oldHead);
    if (oldHead->next.load() == nullptr) {
        head_.store(nullptr);
        tail_.store(nullptr);
        name2ctx_.clear();
    } else {
        oldHead->next.load()->prev.store(nullptr);
        head_.store(oldHead->next.load());
        name2ctx_.erase(oldHead->getName());
    }
    callAfterRemove(oldHead);
    return oldHead->getHandler();
}

ChannelHandler* DefaultChannelPipeline::removeLast() {
    std::lock_guard<std::recursive_mutex> g(lock_);
    if (name2ctx_.empty()) throw ::jlang::NoSuchElementException();
    PipelineContext* oldTail = tail_.load();
    if (oldTail == nullptr) throw ::jlang::NoSuchElementException();
    callBeforeRemove(oldTail);
    if (oldTail->prev.load() == nullptr) {
        head_.store(nullptr);
        tail_.store(nullptr);
        name2ctx_.clear();
    } else {
        oldTail->prev.load()->next.store(nullptr);
        tail_.store(oldTail->prev.load());
        name2ctx_.erase(oldTail->getName());
    }
    callAfterRemove(oldTail);  // Netty 3.2.0.BETA1 calls beforeRemove twice here (bug); fixed
    return oldTail->getHandler();
}

void DefaultChannelPipeline::replace(ChannelHandler* oldHandler, const String& newName, ChannelHandler* newHandler) {
    std::lock_guard<std::recursive_mutex> g(lock_);
    replaceContext(contextOrDie(oldHandler), newName, newHandler);
}

ChannelHandler* DefaultChannelPipeline::replace(const String& oldName, const String& newName,
                                                ChannelHandler* newHandler) {
    std::lock_guard<std::recursive_mutex> g(lock_);
    return replaceContext(contextOrDie(oldName), newName, newHandler);
}

ChannelHandler* DefaultChannelPipeline::replaceContext(PipelineContext* ctx, const String& newName,
                                                       ChannelHandler* newHandler) {
    if (ctx == head_.load()) {
        removeFirst();
        addFirst(newName, newHandler);
    } else if (ctx == tail_.load()) {
        removeLast();
        addLast(newName, newHandler);
    } else {
        bool sameName = ctx->getName().equals(newName);
        if (!sameName) checkDuplicateName(newName);
        PipelineContext* prev = ctx->prev.load();
        PipelineContext* next = ctx->next.load();
        auto* newCtx = new PipelineContext(this, prev, next, newName, newHandler);
        callBeforeRemove(ctx);
        callBeforeAdd(newCtx);
        prev->next.store(newCtx);
        next->prev.store(newCtx);
        if (!sameName) {
            name2ctx_.erase(ctx->getName());
        }
        name2ctx_[newName] = newCtx;
        callAfterRemove(ctx);
        callAfterAdd(newCtx);
    }
    return ctx->getHandler();
}

void DefaultChannelPipeline::callBeforeAdd(PipelineContext* ctx) {
    auto* h = dynamic_cast<LifeCycleAwareChannelHandler*>(ctx->getHandler());
    if (h == nullptr) return;
    try {
        h->beforeAdd(ctx);
    } catch (Throwable& t) {
        throw ChannelHandlerLifeCycleException(
            ::jlang::str(h->getClass()->getName(), ".beforeAdd() has thrown an exception; not adding."), t);
    }
}

void DefaultChannelPipeline::callAfterAdd(PipelineContext* ctx) {
    auto* h = dynamic_cast<LifeCycleAwareChannelHandler*>(ctx->getHandler());
    if (h == nullptr) return;
    try {
        h->afterAdd(ctx);
    } catch (Throwable& t) {
        bool removed = false;
        try {
            removeContext(ctx);
            removed = true;
        } catch (Throwable& t2) {
            detail::logWarn("org.jboss.netty.channel.DefaultChannelPipeline",
                            ::jlang::str("Failed to remove a handler: ", ctx->getName()), &t2);
        }
        throw ChannelHandlerLifeCycleException(
            ::jlang::str(h->getClass()->getName(),
                         removed ? ".afterAdd() has thrown an exception; removed."
                                 : ".afterAdd() has thrown an exception; also failed to remove."),
            t);
    }
}

void DefaultChannelPipeline::callBeforeRemove(PipelineContext* ctx) {
    auto* h = dynamic_cast<LifeCycleAwareChannelHandler*>(ctx->getHandler());
    if (h == nullptr) return;
    try {
        h->beforeRemove(ctx);
    } catch (Throwable& t) {
        throw ChannelHandlerLifeCycleException(
            ::jlang::str(h->getClass()->getName(), ".beforeRemove() has thrown an exception; not removing."), t);
    }
}

void DefaultChannelPipeline::callAfterRemove(PipelineContext* ctx) {
    auto* h = dynamic_cast<LifeCycleAwareChannelHandler*>(ctx->getHandler());
    if (h == nullptr) return;
    try {
        h->afterRemove(ctx);
    } catch (Throwable& t) {
        throw ChannelHandlerLifeCycleException(
            ::jlang::str(h->getClass()->getName(), ".afterRemove() has thrown an exception."), t);
    }
}

ChannelHandler* DefaultChannelPipeline::getFirst() {
    std::lock_guard<std::recursive_mutex> g(lock_);
    PipelineContext* h = head_.load();
    return h == nullptr ? nullptr : h->getHandler();
}

ChannelHandler* DefaultChannelPipeline::getLast() {
    std::lock_guard<std::recursive_mutex> g(lock_);
    PipelineContext* t = tail_.load();
    return t == nullptr ? nullptr : t->getHandler();
}

ChannelHandler* DefaultChannelPipeline::get(const String& name) {
    std::lock_guard<std::recursive_mutex> g(lock_);
    auto it = name2ctx_.find(name);
    return it == name2ctx_.end() ? nullptr : it->second->getHandler();
}

ChannelHandlerContext* DefaultChannelPipeline::getContext(const String& name) {
    if (name.isNull()) throw ::jlang::NullPointerException("name");
    std::lock_guard<std::recursive_mutex> g(lock_);
    auto it = name2ctx_.find(name);
    return it == name2ctx_.end() ? nullptr : it->second;
}

ChannelHandlerContext* DefaultChannelPipeline::getContext(ChannelHandler* handler) {
    if (handler == nullptr) throw ::jlang::NullPointerException("handler");
    std::lock_guard<std::recursive_mutex> g(lock_);
    for (PipelineContext* ctx = head_.load(); ctx != nullptr; ctx = ctx->next.load()) {
        if (ctx->getHandler() == handler) return ctx;
    }
    return nullptr;
}

std::vector<String> DefaultChannelPipeline::getNames() {
    std::lock_guard<std::recursive_mutex> g(lock_);
    std::vector<String> names;
    for (PipelineContext* ctx = head_.load(); ctx != nullptr; ctx = ctx->next.load()) names.push_back(ctx->getName());
    return names;
}

String DefaultChannelPipeline::toString() {
    std::lock_guard<std::recursive_mutex> g(lock_);
    String buf("DefaultChannelPipeline{");
    for (PipelineContext* ctx = head_.load(); ctx != nullptr; ctx = ctx->next.load()) {
        buf += ::jlang::str("(", ctx->getName(), " = ", ctx->getHandler()->getClass()->getName(), ")");
        if (ctx->next.load() != nullptr) buf += ", ";
    }
    buf += "}";
    return buf;
}

void DefaultChannelPipeline::sendUpstream(ChannelEvent* e) {
    PipelineContext* head = getActualUpstreamContext(head_.load());
    if (head == nullptr) {
        detail::logWarn("org.jboss.netty.channel.DefaultChannelPipeline",
                        ::jlang::str("The pipeline contains no upstream handlers; discarding: ", e));
        return;
    }
    sendUpstream(head, e);
}

void DefaultChannelPipeline::sendUpstream(PipelineContext* ctx, ChannelEvent* e) {
    try {
        ctx->up_->handleUpstream(ctx, e);
    } catch (Throwable& t) {
        notifyHandlerException(e, detail::copyOf(t));
    } catch (std::exception& ex) {
        notifyHandlerException(e, detail::copyOf(ex));
    }
}

void DefaultChannelPipeline::sendDownstream(ChannelEvent* e) {
    PipelineContext* tail = getActualDownstreamContext(tail_.load());
    if (tail == nullptr) {
        try {
            getSink()->eventSunk(this, e);
        } catch (Throwable& t) {
            notifyHandlerException(e, detail::copyOf(t));
        } catch (std::exception& ex) {
            notifyHandlerException(e, detail::copyOf(ex));
        }
        return;
    }
    sendDownstream(tail, e);
}

void DefaultChannelPipeline::sendDownstream(PipelineContext* ctx, ChannelEvent* e) {
    try {
        ctx->down_->handleDownstream(ctx, e);
    } catch (Throwable& t) {
        notifyHandlerException(e, detail::copyOf(t));
    } catch (std::exception& ex) {
        notifyHandlerException(e, detail::copyOf(ex));
    }
}

PipelineContext* DefaultChannelPipeline::getActualUpstreamContext(PipelineContext* ctx) {
    PipelineContext* realCtx = ctx;
    while (realCtx != nullptr && !realCtx->canHandleUpstream()) realCtx = realCtx->next.load();
    return realCtx;
}

PipelineContext* DefaultChannelPipeline::getActualDownstreamContext(PipelineContext* ctx) {
    PipelineContext* realCtx = ctx;
    while (realCtx != nullptr && !realCtx->canHandleDownstream()) realCtx = realCtx->prev.load();
    return realCtx;
}

void DefaultChannelPipeline::notifyHandlerException(ChannelEvent* e, Throwable* t) {
    if (dynamic_cast<ExceptionEvent*>(e) != nullptr) {
        detail::logWarn("org.jboss.netty.channel.DefaultChannelPipeline",
                        ::jlang::str("An exception was thrown by a user handler while handling an exception event (",
                                     e, ")"),
                        t);
        return;
    }
    try {
        if (auto* pe = dynamic_cast<ChannelPipelineException*>(t)) {
            getSink()->exceptionCaught(this, e, *pe);
        } else {
            ChannelPipelineException wrapped(t);
            getSink()->exceptionCaught(this, e, wrapped);
        }
    } catch (Throwable& e1) {
        detail::logWarn("org.jboss.netty.channel.DefaultChannelPipeline",
                        "An exception was thrown by an exception handler.", &e1);
    }
}

void DefaultChannelPipeline::init(const String& name, ChannelHandler* handler) {
    auto* ctx = new PipelineContext(this, nullptr, nullptr, name, handler);
    callBeforeAdd(ctx);
    head_.store(ctx);
    tail_.store(ctx);
    name2ctx_.clear();
    name2ctx_[name] = ctx;
    callAfterAdd(ctx);
}

void DefaultChannelPipeline::checkDuplicateName(const String& name) {
    if (name2ctx_.count(name) != 0) throw ::jlang::IllegalArgumentException("Duplicate handler name.");
}

PipelineContext* DefaultChannelPipeline::contextOrDie(const String& name) {
    auto* ctx = static_cast<PipelineContext*>(dynamic_cast<PipelineContext*>(getContext(name)));
    if (ctx == nullptr) throw ::jlang::NoSuchElementException(name);
    return ctx;
}

PipelineContext* DefaultChannelPipeline::contextOrDie(ChannelHandler* handler) {
    auto* ctx = dynamic_cast<PipelineContext*>(getContext(handler));
    if (ctx == nullptr) throw ::jlang::NoSuchElementException(handler->getClass()->getName());
    return ctx;
}

// =======================================================================================
// SimpleChannelUpstreamHandler / SimpleChannelDownstreamHandler
// =======================================================================================

void SimpleChannelUpstreamHandler::handleUpstream(ChannelHandlerContext* ctx, ChannelEvent* e) {
    if (auto* me = dynamic_cast<MessageEvent*>(e)) {
        messageReceived(ctx, me);
    } else if (auto* wc = dynamic_cast<WriteCompletionEvent*>(e)) {
        writeComplete(ctx, wc);
    } else if (auto* ce = dynamic_cast<ChildChannelStateEvent*>(e)) {
        if (ce->getChildChannel()->isOpen()) {
            childChannelOpen(ctx, ce);
        } else {
            childChannelClosed(ctx, ce);
        }
    } else if (auto* se = dynamic_cast<ChannelStateEvent*>(e)) {
        switch (se->getState()) {
        case ChannelState::OPEN:
            if (detail::isTrue(se->getValue())) {
                channelOpen(ctx, se);
            } else {
                channelClosed(ctx, se);
            }
            break;
        case ChannelState::BOUND:
            if (se->getValue() != nullptr) {
                channelBound(ctx, se);
            } else {
                channelUnbound(ctx, se);
            }
            break;
        case ChannelState::CONNECTED:
            if (se->getValue() != nullptr) {
                channelConnected(ctx, se);
            } else {
                channelDisconnected(ctx, se);
            }
            break;
        case ChannelState::INTEREST_OPS: channelInterestChanged(ctx, se); break;
        default: ctx->sendUpstream(e);
        }
    } else if (auto* ee = dynamic_cast<ExceptionEvent*>(e)) {
        exceptionCaught(ctx, ee);
    } else {
        ctx->sendUpstream(e);
    }
}

void SimpleChannelUpstreamHandler::messageReceived(ChannelHandlerContext* ctx, MessageEvent* e) { ctx->sendUpstream(e); }

void SimpleChannelUpstreamHandler::exceptionCaught(ChannelHandlerContext* ctx, ExceptionEvent* e) {
    if (static_cast<ChannelHandler*>(this) == ctx->getPipeline()->getLast()) {
        detail::logWarn("org.jboss.netty.channel.SimpleChannelUpstreamHandler",
                        ::jlang::str("EXCEPTION, please implement ", getClass()->getName(),
                                     ".exceptionCaught() for proper handling."),
                        e->getCause());
    }
    ctx->sendUpstream(e);
}

void SimpleChannelUpstreamHandler::channelOpen(ChannelHandlerContext* ctx, ChannelStateEvent* e) { ctx->sendUpstream(e); }
void SimpleChannelUpstreamHandler::channelBound(ChannelHandlerContext* ctx, ChannelStateEvent* e) { ctx->sendUpstream(e); }
void SimpleChannelUpstreamHandler::channelConnected(ChannelHandlerContext* ctx, ChannelStateEvent* e) {
    ctx->sendUpstream(e);
}
void SimpleChannelUpstreamHandler::channelInterestChanged(ChannelHandlerContext* ctx, ChannelStateEvent* e) {
    ctx->sendUpstream(e);
}
void SimpleChannelUpstreamHandler::channelDisconnected(ChannelHandlerContext* ctx, ChannelStateEvent* e) {
    ctx->sendUpstream(e);
}
void SimpleChannelUpstreamHandler::channelUnbound(ChannelHandlerContext* ctx, ChannelStateEvent* e) {
    ctx->sendUpstream(e);
}
void SimpleChannelUpstreamHandler::channelClosed(ChannelHandlerContext* ctx, ChannelStateEvent* e) {
    ctx->sendUpstream(e);
}
void SimpleChannelUpstreamHandler::writeComplete(ChannelHandlerContext* ctx, WriteCompletionEvent* e) {
    ctx->sendUpstream(e);
}
void SimpleChannelUpstreamHandler::childChannelOpen(ChannelHandlerContext* ctx, ChildChannelStateEvent* e) {
    ctx->sendUpstream(e);
}
void SimpleChannelUpstreamHandler::childChannelClosed(ChannelHandlerContext* ctx, ChildChannelStateEvent* e) {
    ctx->sendUpstream(e);
}

void SimpleChannelDownstreamHandler::handleDownstream(ChannelHandlerContext* ctx, ChannelEvent* e) {
    if (auto* me = dynamic_cast<MessageEvent*>(e)) {
        writeRequested(ctx, me);
    } else if (auto* se = dynamic_cast<ChannelStateEvent*>(e)) {
        switch (se->getState()) {
        case ChannelState::OPEN:
            if (!detail::isTrue(se->getValue())) closeRequested(ctx, se);
            break;
        case ChannelState::BOUND:
            if (se->getValue() != nullptr) {
                bindRequested(ctx, se);
            } else {
                unbindRequested(ctx, se);
            }
            break;
        case ChannelState::CONNECTED:
            if (se->getValue() != nullptr) {
                connectRequested(ctx, se);
            } else {
                disconnectRequested(ctx, se);
            }
            break;
        case ChannelState::INTEREST_OPS: setInterestOpsRequested(ctx, se); break;
        default: ctx->sendDownstream(e);
        }
    } else {
        ctx->sendDownstream(e);
    }
}

void SimpleChannelDownstreamHandler::writeRequested(ChannelHandlerContext* ctx, MessageEvent* e) { ctx->sendDownstream(e); }
void SimpleChannelDownstreamHandler::bindRequested(ChannelHandlerContext* ctx, ChannelStateEvent* e) {
    ctx->sendDownstream(e);
}
void SimpleChannelDownstreamHandler::connectRequested(ChannelHandlerContext* ctx, ChannelStateEvent* e) {
    ctx->sendDownstream(e);
}
void SimpleChannelDownstreamHandler::setInterestOpsRequested(ChannelHandlerContext* ctx, ChannelStateEvent* e) {
    ctx->sendDownstream(e);
}
void SimpleChannelDownstreamHandler::disconnectRequested(ChannelHandlerContext* ctx, ChannelStateEvent* e) {
    ctx->sendDownstream(e);
}
void SimpleChannelDownstreamHandler::unbindRequested(ChannelHandlerContext* ctx, ChannelStateEvent* e) {
    ctx->sendDownstream(e);
}
void SimpleChannelDownstreamHandler::closeRequested(ChannelHandlerContext* ctx, ChannelStateEvent* e) {
    ctx->sendDownstream(e);
}

// =======================================================================================
// Futures
// =======================================================================================
namespace {
std::atomic<bool> g_useDeadLockChecker{true};

class CloseListener final : public ChannelFutureListener {
public:
    void operationComplete(ChannelFuture* future) override { future->getChannel()->close(); }
};
class CloseOnFailureListener final : public ChannelFutureListener {
public:
    void operationComplete(ChannelFuture* future) override {
        if (!future->isSuccess()) future->getChannel()->close();
    }
};
// Static storage: the pointers below are constant-initialized (usable at any time).
CloseListener g_closeListener;
CloseOnFailureListener g_closeOnFailureListener;

void currentThreadInterrupt() { ::jlang::Thread::currentThread()->interrupt(); }
bool currentThreadInterrupted() { return ::jlang::Thread::interrupted(); }

void warnListenerException(Throwable* t) {
    detail::logWarn("org.jboss.netty.channel.DefaultChannelFuture",
                    "An exception was thrown by ChannelFutureListener.", t);
}
}  // namespace

ChannelFutureListener* const ChannelFutureListener::CLOSE = &g_closeListener;
ChannelFutureListener* const ChannelFutureListener::CLOSE_ON_FAILURE = &g_closeOnFailureListener;

bool DefaultChannelFuture::isUseDeadLockChecker() { return g_useDeadLockChecker.load(); }
void DefaultChannelFuture::setUseDeadLockChecker(bool useDeadLockChecker) { g_useDeadLockChecker.store(useDeadLockChecker); }

DefaultChannelFuture::DefaultChannelFuture(Channel* channel, bool cancellable)
    : channel_(channel), cancellable_(cancellable) {}

bool DefaultChannelFuture::isDone() {
    JSYNC(this) { return done_; }
}

bool DefaultChannelFuture::isSuccess() {
    JSYNC(this) { return done_ && cause_ == nullptr && !cancelled_; }
}

Throwable* DefaultChannelFuture::getCause() {
    JSYNC(this) { return cancelled_ ? nullptr : cause_; }
}

bool DefaultChannelFuture::isCancelled() {
    JSYNC(this) { return cancelled_; }
}

void DefaultChannelFuture::addListener(ChannelFutureListener* listener) {
    if (listener == nullptr) throw ::jlang::NullPointerException("listener");
    bool notifyNow = false;
    JSYNC(this) {
        if (done_) {
            notifyNow = true;
        } else {
            if (firstListener_ == nullptr) {
                firstListener_ = listener;
            } else {
                otherListeners_.push_back(listener);
            }
            if (auto* pl = dynamic_cast<ChannelFutureProgressListener*>(listener)) progressListeners_.push_back(pl);
        }
    }
    if (notifyNow) notifyListener(listener);
}

void DefaultChannelFuture::removeListener(ChannelFutureListener* listener) {
    if (listener == nullptr) throw ::jlang::NullPointerException("listener");
    JSYNC(this) {
        if (!done_) {
            if (listener == firstListener_) {
                if (!otherListeners_.empty()) {
                    firstListener_ = otherListeners_.front();
                    otherListeners_.erase(otherListeners_.begin());
                } else {
                    firstListener_ = nullptr;
                }
            } else {
                for (auto it = otherListeners_.begin(); it != otherListeners_.end(); ++it) {
                    if (*it == listener) {
                        otherListeners_.erase(it);
                        break;
                    }
                }
            }
            if (auto* pl = dynamic_cast<ChannelFutureProgressListener*>(listener)) {
                for (auto it = progressListeners_.begin(); it != progressListeners_.end(); ++it) {
                    if (*it == pl) {
                        progressListeners_.erase(it);
                        break;
                    }
                }
            }
        }
    }
}

ChannelFuture* DefaultChannelFuture::await() {
    if (currentThreadInterrupted()) throw ::jlang::InterruptedException();
    JSYNC(this) {
        while (!done_) {
            checkDeadLock();
            waiters_++;
            try {
                wait();
            } catch (...) {
                waiters_--;
                throw;
            }
            waiters_--;
        }
    }
    return this;
}

ChannelFuture* DefaultChannelFuture::awaitUninterruptibly() {
    bool interrupted = false;
    JSYNC(this) {
        while (!done_) {
            checkDeadLock();
            waiters_++;
            try {
                wait();
            } catch (::jlang::InterruptedException&) {
                interrupted = true;
            }
            waiters_--;
        }
    }
    if (interrupted) currentThreadInterrupt();
    return this;
}

bool DefaultChannelFuture::await(int64_t timeout, ::jlang::TimeUnit unit) { return await0(unit.toNanos(timeout), true); }
bool DefaultChannelFuture::await(int64_t timeoutMillis) { return await0(timeoutMillis * 1000000, true); }
bool DefaultChannelFuture::awaitUninterruptibly(int64_t timeout, ::jlang::TimeUnit unit) {
    return await0(unit.toNanos(timeout), false);
}
bool DefaultChannelFuture::awaitUninterruptibly(int64_t timeoutMillis) { return await0(timeoutMillis * 1000000, false); }

bool DefaultChannelFuture::await0(int64_t timeoutNanos, bool interruptable) {
    if (interruptable && currentThreadInterrupted()) throw ::jlang::InterruptedException();
    int64_t startTime = timeoutNanos <= 0 ? 0 : detail::nanoTime();
    int64_t waitTime = timeoutNanos;
    bool interrupted = false;
    bool result = false;
    JSYNC(this) {
        if (done_ || waitTime <= 0) {
            result = done_;
        } else {
            checkDeadLock();
            waiters_++;
            for (;;) {
                try {
                    wait(waitTime / 1000000, static_cast<int32_t>(waitTime % 1000000));
                } catch (::jlang::InterruptedException&) {
                    if (interruptable) {
                        waiters_--;
                        throw;
                    }
                    interrupted = true;
                }
                if (done_) {
                    result = true;
                    break;
                }
                waitTime = timeoutNanos - (detail::nanoTime() - startTime);
                if (waitTime <= 0) {
                    result = done_;
                    break;
                }
            }
            waiters_--;
        }
    }
    if (interrupted) currentThreadInterrupt();
    return result;
}

void DefaultChannelFuture::checkDeadLock() {
    if (isUseDeadLockChecker() && detail::inIoThread()) {
        throw ::jlang::IllegalStateException(
            "await*() in I/O thread causes a dead lock or sudden performance drop. Use addListener() instead or "
            "call await*() from a different thread.");
    }
}

bool DefaultChannelFuture::setSuccess() {
    JSYNC(this) {
        if (done_) return false;
        done_ = true;
        if (waiters_ > 0) notifyAll();
    }
    notifyListeners();
    return true;
}

bool DefaultChannelFuture::setFailure(Throwable* cause) {
    JSYNC(this) {
        if (done_) return false;
        cause_ = cause;
        done_ = true;
        if (waiters_ > 0) notifyAll();
    }
    notifyListeners();
    return true;
}

bool DefaultChannelFuture::cancel() {
    if (!cancellable_) return false;
    JSYNC(this) {
        if (done_) return false;
        cancelled_ = true;
        done_ = true;
        if (waiters_ > 0) notifyAll();
    }
    notifyListeners();
    return true;
}

void DefaultChannelFuture::notifyListeners() {
    // done_ is set: no listener can be added any more (addListener notifies directly).
    ChannelFutureListener* first = nullptr;
    std::vector<ChannelFutureListener*> others;
    JSYNC(this) {
        first = firstListener_;
        firstListener_ = nullptr;
        others.swap(otherListeners_);
    }
    if (first != nullptr) {
        notifyListener(first);
        for (ChannelFutureListener* l : others) notifyListener(l);
    }
}

void DefaultChannelFuture::notifyListener(ChannelFutureListener* l) {
    try {
        l->operationComplete(this);
    } catch (Throwable& t) {
        warnListenerException(&t);
    } catch (std::exception& e) {
        warnListenerException(detail::copyOf(e));
    }
}

bool DefaultChannelFuture::setProgress(int64_t amount, int64_t current, int64_t total) {
    std::vector<ChannelFutureProgressListener*> plisteners;
    JSYNC(this) {
        if (done_) return false;
        if (progressListeners_.empty()) return true;
        plisteners = progressListeners_;
    }
    for (ChannelFutureProgressListener* pl : plisteners) {
        try {
            pl->operationProgressed(this, amount, current, total);
        } catch (Throwable& t) {
            detail::logWarn("org.jboss.netty.channel.DefaultChannelFuture",
                            "An exception was thrown by ChannelFutureProgressListener.", &t);
        }
    }
    return true;
}

CompleteChannelFuture::CompleteChannelFuture(Channel* channel) : channel_(channel) {
    if (channel == nullptr) throw ::jlang::NullPointerException("channel");
}

void CompleteChannelFuture::addListener(ChannelFutureListener* listener) {
    try {
        listener->operationComplete(this);
    } catch (Throwable& t) {
        detail::logWarn("org.jboss.netty.channel.CompleteChannelFuture",
                        "An exception was thrown by ChannelFutureListener.", &t);
    }
}

ChannelFuture* CompleteChannelFuture::await() {
    if (currentThreadInterrupted()) throw ::jlang::InterruptedException();
    return this;
}

bool CompleteChannelFuture::await(int64_t, ::jlang::TimeUnit) {
    if (currentThreadInterrupted()) throw ::jlang::InterruptedException();
    return true;
}

bool CompleteChannelFuture::await(int64_t) {
    if (currentThreadInterrupted()) throw ::jlang::InterruptedException();
    return true;
}

FailedChannelFuture::FailedChannelFuture(Channel* channel, Throwable* cause) : CompleteChannelFuture(channel), cause_(cause) {
    if (cause == nullptr) throw ::jlang::NullPointerException("cause");
}

// =======================================================================================
// Channels
// =======================================================================================

ChannelPipeline* Channels::pipeline() { return new DefaultChannelPipeline(); }

ChannelPipeline* Channels::pipeline(::jlang::Array<ChannelHandler*>* handlers) {
    if (handlers == nullptr) throw ::jlang::NullPointerException("handlers");
    ChannelPipeline* newPipeline = pipeline();
    for (int32_t i = 0; i < handlers->length; i++) {
        ChannelHandler* h = (*handlers)[i];
        if (h == nullptr) break;
        newPipeline->addLast(String::valueOf(i), h);
    }
    return newPipeline;
}

ChannelPipeline* Channels::pipelineOf(std::initializer_list<ChannelHandler*> handlers) {
    ChannelPipeline* newPipeline = pipeline();
    int32_t i = 0;
    for (ChannelHandler* h : handlers) {
        if (h == nullptr) break;
        newPipeline->addLast(String::valueOf(i++), h);
    }
    return newPipeline;
}

ChannelPipeline* Channels::pipeline(ChannelPipeline* pipeline) {
    ChannelPipeline* newPipeline = Channels::pipeline();
    for (const String& name : pipeline->getNames()) {
        ChannelHandler* h = pipeline->get(name);
        if (h != nullptr) newPipeline->addLast(name, h);
    }
    return newPipeline;
}

ChannelPipelineFactory* Channels::pipelineFactory(ChannelPipeline* pipeline) {
    return ChannelPipelineFactory::of([pipeline]() { return Channels::pipeline(pipeline); });
}

ChannelFuture* Channels::future(Channel* channel) { return future(channel, false); }
ChannelFuture* Channels::future(Channel* channel, bool cancellable) { return new DefaultChannelFuture(channel, cancellable); }

ChannelFuture* Channels::succeededFuture(Channel* channel) {
    if (auto* ac = dynamic_cast<AbstractChannel*>(channel)) return ac->getSucceededFuture();
    return new SucceededChannelFuture(channel);
}

ChannelFuture* Channels::failedFuture(Channel* channel, Throwable* cause) { return new FailedChannelFuture(channel, cause); }

void Channels::fireChannelOpen(Channel* channel) {
    if (channel->getParent() != nullptr) {
        channel->getParent()->getPipeline()->sendUpstream(new DefaultChildChannelStateEvent(channel->getParent(), channel));
    }
    channel->getPipeline()->sendUpstream(new UpstreamChannelStateEvent(channel, ChannelState::OPEN, detail::boxBool(true)));
}
void Channels::fireChannelOpen(ChannelHandlerContext* ctx) {
    ctx->sendUpstream(new UpstreamChannelStateEvent(ctx->getChannel(), ChannelState::OPEN, detail::boxBool(true)));
}
void Channels::fireChannelBound(Channel* channel, InetSocketAddress* localAddress) {
    channel->getPipeline()->sendUpstream(new UpstreamChannelStateEvent(channel, ChannelState::BOUND, localAddress));
}
void Channels::fireChannelBound(ChannelHandlerContext* ctx, InetSocketAddress* localAddress) {
    ctx->sendUpstream(new UpstreamChannelStateEvent(ctx->getChannel(), ChannelState::BOUND, localAddress));
}
void Channels::fireChannelConnected(Channel* channel, InetSocketAddress* remoteAddress) {
    channel->getPipeline()->sendUpstream(new UpstreamChannelStateEvent(channel, ChannelState::CONNECTED, remoteAddress));
}
void Channels::fireChannelConnected(ChannelHandlerContext* ctx, InetSocketAddress* remoteAddress) {
    ctx->sendUpstream(new UpstreamChannelStateEvent(ctx->getChannel(), ChannelState::CONNECTED, remoteAddress));
}
void Channels::fireMessageReceived(Channel* channel, Object* message) { fireMessageReceived(channel, message, nullptr); }
void Channels::fireMessageReceived(Channel* channel, Object* message, InetSocketAddress* remoteAddress) {
    channel->getPipeline()->sendUpstream(new UpstreamMessageEvent(channel, message, remoteAddress));
}
void Channels::fireMessageReceived(ChannelHandlerContext* ctx, Object* message) {
    ctx->sendUpstream(new UpstreamMessageEvent(ctx->getChannel(), message, nullptr));
}
void Channels::fireMessageReceived(ChannelHandlerContext* ctx, Object* message, InetSocketAddress* remoteAddress) {
    ctx->sendUpstream(new UpstreamMessageEvent(ctx->getChannel(), message, remoteAddress));
}
void Channels::fireWriteComplete(Channel* channel, int64_t amount) {
    if (amount == 0) return;
    channel->getPipeline()->sendUpstream(new DefaultWriteCompletionEvent(channel, amount));
}
void Channels::fireWriteComplete(ChannelHandlerContext* ctx, int64_t amount) {
    ctx->sendUpstream(new DefaultWriteCompletionEvent(ctx->getChannel(), amount));
}
void Channels::fireChannelInterestChanged(Channel* channel) {
    channel->getPipeline()->sendUpstream(
        new UpstreamChannelStateEvent(channel, ChannelState::INTEREST_OPS, detail::boxInt(Channel::OP_READ)));
}
void Channels::fireChannelInterestChanged(ChannelHandlerContext* ctx) {
    ctx->sendUpstream(
        new UpstreamChannelStateEvent(ctx->getChannel(), ChannelState::INTEREST_OPS, detail::boxInt(Channel::OP_READ)));
}
void Channels::fireChannelDisconnected(Channel* channel) {
    channel->getPipeline()->sendUpstream(new UpstreamChannelStateEvent(channel, ChannelState::CONNECTED, nullptr));
}
void Channels::fireChannelDisconnected(ChannelHandlerContext* ctx) {
    ctx->sendUpstream(new UpstreamChannelStateEvent(ctx->getChannel(), ChannelState::CONNECTED, nullptr));
}
void Channels::fireChannelUnbound(Channel* channel) {
    channel->getPipeline()->sendUpstream(new UpstreamChannelStateEvent(channel, ChannelState::BOUND, nullptr));
}
void Channels::fireChannelUnbound(ChannelHandlerContext* ctx) {
    ctx->sendUpstream(new UpstreamChannelStateEvent(ctx->getChannel(), ChannelState::BOUND, nullptr));
}
void Channels::fireChannelClosed(Channel* channel) {
    channel->getPipeline()->sendUpstream(new UpstreamChannelStateEvent(channel, ChannelState::OPEN, detail::boxBool(false)));
    if (channel->getParent() != nullptr) {
        channel->getParent()->getPipeline()->sendUpstream(new DefaultChildChannelStateEvent(channel->getParent(), channel));
    }
}
void Channels::fireChannelClosed(ChannelHandlerContext* ctx) {
    ctx->sendUpstream(new UpstreamChannelStateEvent(ctx->getChannel(), ChannelState::OPEN, detail::boxBool(false)));
}
void Channels::fireExceptionCaught(Channel* channel, Throwable* cause) {
    channel->getPipeline()->sendUpstream(new DefaultExceptionEvent(channel, cause));
}
void Channels::fireExceptionCaught(ChannelHandlerContext* ctx, Throwable* cause) {
    ctx->sendUpstream(new DefaultExceptionEvent(ctx->getChannel(), cause));
}

ChannelFuture* Channels::bind(Channel* channel, InetSocketAddress* localAddress) {
    if (localAddress == nullptr) throw ::jlang::NullPointerException("localAddress");
    ChannelFuture* f = future(channel);
    channel->getPipeline()->sendDownstream(new DownstreamChannelStateEvent(channel, f, ChannelState::BOUND, localAddress));
    return f;
}
void Channels::bind(ChannelHandlerContext* ctx, ChannelFuture* future, InetSocketAddress* localAddress) {
    if (localAddress == nullptr) throw ::jlang::NullPointerException("localAddress");
    ctx->sendDownstream(new DownstreamChannelStateEvent(ctx->getChannel(), future, ChannelState::BOUND, localAddress));
}
ChannelFuture* Channels::unbind(Channel* channel) {
    ChannelFuture* f = future(channel);
    channel->getPipeline()->sendDownstream(new DownstreamChannelStateEvent(channel, f, ChannelState::BOUND, nullptr));
    return f;
}
void Channels::unbind(ChannelHandlerContext* ctx, ChannelFuture* future) {
    ctx->sendDownstream(new DownstreamChannelStateEvent(ctx->getChannel(), future, ChannelState::BOUND, nullptr));
}
ChannelFuture* Channels::connect(Channel* channel, InetSocketAddress* remoteAddress) {
    if (remoteAddress == nullptr) throw ::jlang::NullPointerException("remoteAddress");
    ChannelFuture* f = future(channel, true);
    channel->getPipeline()->sendDownstream(
        new DownstreamChannelStateEvent(channel, f, ChannelState::CONNECTED, remoteAddress));
    return f;
}
void Channels::connect(ChannelHandlerContext* ctx, ChannelFuture* future, InetSocketAddress* remoteAddress) {
    if (remoteAddress == nullptr) throw ::jlang::NullPointerException("remoteAddress");
    ctx->sendDownstream(
        new DownstreamChannelStateEvent(ctx->getChannel(), future, ChannelState::CONNECTED, remoteAddress));
}
ChannelFuture* Channels::write(Channel* channel, Object* message) { return write(channel, message, nullptr); }
ChannelFuture* Channels::write(Channel* channel, Object* message, InetSocketAddress* remoteAddress) {
    ChannelFuture* f = future(channel);
    channel->getPipeline()->sendDownstream(new DownstreamMessageEvent(channel, f, message, remoteAddress));
    return f;
}
void Channels::write(ChannelHandlerContext* ctx, ChannelFuture* future, Object* message) {
    write(ctx, future, message, nullptr);
}
void Channels::write(ChannelHandlerContext* ctx, ChannelFuture* future, Object* message,
                     InetSocketAddress* remoteAddress) {
    ctx->sendDownstream(new DownstreamMessageEvent(ctx->getChannel(), future, message, remoteAddress));
}

namespace {
void validateInterestOps(int32_t interestOps) {
    switch (interestOps) {
    case Channel::OP_NONE:
    case Channel::OP_READ:
    case Channel::OP_WRITE:
    case Channel::OP_READ_WRITE: break;
    default: throw ::jlang::IllegalArgumentException(::jlang::str("Invalid interestOps: ", interestOps));
    }
}
int32_t filterDownstreamInterestOps(int32_t interestOps) { return interestOps & ~Channel::OP_WRITE; }
}  // namespace

ChannelFuture* Channels::setInterestOps(Channel* channel, int32_t interestOps) {
    validateInterestOps(interestOps);
    interestOps = filterDownstreamInterestOps(interestOps);
    ChannelFuture* f = future(channel);
    channel->getPipeline()->sendDownstream(
        new DownstreamChannelStateEvent(channel, f, ChannelState::INTEREST_OPS, detail::boxInt(interestOps)));
    return f;
}
void Channels::setInterestOps(ChannelHandlerContext* ctx, ChannelFuture* future, int32_t interestOps) {
    validateInterestOps(interestOps);
    interestOps = filterDownstreamInterestOps(interestOps);
    ctx->sendDownstream(
        new DownstreamChannelStateEvent(ctx->getChannel(), future, ChannelState::INTEREST_OPS, detail::boxInt(interestOps)));
}
ChannelFuture* Channels::disconnect(Channel* channel) {
    ChannelFuture* f = future(channel);
    channel->getPipeline()->sendDownstream(new DownstreamChannelStateEvent(channel, f, ChannelState::CONNECTED, nullptr));
    return f;
}
void Channels::disconnect(ChannelHandlerContext* ctx, ChannelFuture* future) {
    ctx->sendDownstream(new DownstreamChannelStateEvent(ctx->getChannel(), future, ChannelState::CONNECTED, nullptr));
}
ChannelFuture* Channels::close(Channel* channel) {
    ChannelFuture* f = channel->getCloseFuture();
    channel->getPipeline()->sendDownstream(
        new DownstreamChannelStateEvent(channel, f, ChannelState::OPEN, detail::boxBool(false)));
    return f;
}
void Channels::close(ChannelHandlerContext* ctx, ChannelFuture* future) {
    ctx->sendDownstream(new DownstreamChannelStateEvent(ctx->getChannel(), future, ChannelState::OPEN, detail::boxBool(false)));
}

// =======================================================================================
// AbstractChannel
// =======================================================================================
namespace {

std::mutex g_allChannelsLock;
std::unordered_map<int32_t, Channel*>* allChannels() {
    static auto* m = new std::unordered_map<int32_t, Channel*>();
    return m;
}

int32_t allocateId(Channel* channel) {
    int32_t id = channel->identityHashCode();
    std::lock_guard<std::mutex> g(g_allChannelsLock);
    auto* m = allChannels();
    while (m->count(id) != 0) id++;
    (*m)[id] = channel;
    return id;
}

// AbstractChannel.ChannelCloseFuture: setSuccess/setFailure are ignored; setClosed completes it.
class ChannelCloseFuture final : public DefaultChannelFuture {
public:
    explicit ChannelCloseFuture(Channel* channel) : DefaultChannelFuture(channel, false) {}
    bool setSuccess() override { return false; }
    bool setFailure(Throwable*) override { return false; }
    bool setClosed() { return DefaultChannelFuture::setSuccess(); }
};

class IdDeallocator final : public ChannelFutureListener {
public:
    void operationComplete(ChannelFuture* future) override {
        std::lock_guard<std::mutex> g(g_allChannelsLock);
        allChannels()->erase(future->getChannel()->getId());
    }
};
IdDeallocator g_idDeallocator;

}  // namespace

AbstractChannel::AbstractChannel(Channel* parent, ChannelFactory* factory, ChannelPipeline* pipeline, ChannelSink* sink)
    : parent_(parent), factory_(factory), pipeline_(pipeline) {
    id_ = allocateId(this);
    succeededFuture_ = new SucceededChannelFuture(this);
    closeFuture_ = new ChannelCloseFuture(this);
    closeFuture_->addListener(&g_idDeallocator);
    pipeline->attach(this, sink);
}

bool AbstractChannel::isOpen() { return !closeFuture_->isDone(); }

bool AbstractChannel::setClosed() { return static_cast<ChannelCloseFuture*>(closeFuture_)->setClosed(); }

ChannelFuture* AbstractChannel::getSucceededFuture() { return succeededFuture_; }
ChannelFuture* AbstractChannel::getCloseFuture() { return closeFuture_; }

ChannelFuture* AbstractChannel::write(Object* message) { return Channels::write(this, message); }
ChannelFuture* AbstractChannel::write(Object* message, InetSocketAddress* remoteAddress) {
    return Channels::write(this, message, remoteAddress);
}
ChannelFuture* AbstractChannel::bind(InetSocketAddress* localAddress) { return Channels::bind(this, localAddress); }
ChannelFuture* AbstractChannel::connect(InetSocketAddress* remoteAddress) { return Channels::connect(this, remoteAddress); }
ChannelFuture* AbstractChannel::disconnect() { return Channels::disconnect(this); }
ChannelFuture* AbstractChannel::unbind() { return Channels::unbind(this); }
ChannelFuture* AbstractChannel::close() {
    Channels::close(this);
    return closeFuture_;
}

ChannelFuture* AbstractChannel::setInterestOps(int32_t interestOps) { return Channels::setInterestOps(this, interestOps); }

ChannelFuture* AbstractChannel::setReadable(bool readable) {
    if (readable) return setInterestOps(getInterestOps() | OP_READ);
    return setInterestOps(getInterestOps() & ~OP_READ);
}

int32_t AbstractChannel::compareTo(Channel* o) {
    int32_t a = getId(), b = o->getId();
    return a < b ? -1 : (a == b ? 0 : 1);
}

String AbstractChannel::toString() {
    bool connected = isConnected();
    {
        std::lock_guard<std::mutex> g(strLock_);
        if (connected && !strVal_.isNull()) return strVal_;
    }
    char hex[16];
    std::snprintf(hex, sizeof hex, "%08x", static_cast<uint32_t>(id_));
    String buf = ::jlang::str("[id: 0x", hex);
    InetSocketAddress* localAddress = getLocalAddress();
    InetSocketAddress* remoteAddress = getRemoteAddress();
    if (remoteAddress != nullptr) {
        buf += ", ";
        if (getParent() == nullptr) {
            buf += ::jlang::str(localAddress, " => ", remoteAddress);
        } else {
            buf += ::jlang::str(remoteAddress, " => ", localAddress);
        }
    } else if (localAddress != nullptr) {
        buf += ::jlang::str(", ", localAddress);
    }
    buf += "]";
    std::lock_guard<std::mutex> g(strLock_);
    strVal_ = connected ? buf : String();
    return buf;
}

// =======================================================================================
// Configs
// =======================================================================================

ChannelConfig::ChannelConfig() { bufferFactory_.store(HeapChannelBufferFactory::getInstance()); }

void ChannelConfig::setOptions(const std::map<String, Object*>& options) {
    for (auto& e : options) setOption(e.first, e.second);
}

bool ChannelConfig::setOption(const String& key, Object* value) {
    if (key.equals("pipelineFactory")) {
        setPipelineFactory(::jlang::cast<ChannelPipelineFactory>(value));
    } else if (key.equals("connectTimeoutMillis")) {
        setConnectTimeoutMillis(detail::toInt(value));
    } else if (key.equals("bufferFactory")) {
        setBufferFactory(::jlang::cast<ChannelBufferFactory>(value));
    } else {
        return false;
    }
    return true;
}

void ChannelConfig::setBufferFactory(ChannelBufferFactory* bufferFactory) {
    if (bufferFactory == nullptr) throw ::jlang::NullPointerException("bufferFactory");
    bufferFactory_.store(bufferFactory);
}

void ChannelConfig::setPipelineFactory(ChannelPipelineFactory* pipelineFactory) {
    if (pipelineFactory == nullptr) throw ::jlang::NullPointerException("pipelineFactory");
    pipelineFactory_.store(pipelineFactory);
}

void ChannelConfig::setConnectTimeoutMillis(int32_t connectTimeoutMillis) {
    if (connectTimeoutMillis < 0) {
        throw ::jlang::IllegalArgumentException(::jlang::str("connectTimeoutMillis: ", connectTimeoutMillis));
    }
    connectTimeoutMillis_.store(connectTimeoutMillis);
}

namespace {
// Socket option errors surface as ChannelException(SocketException), like Netty's configs.
template<class F>
auto socketOption(F f) -> decltype(f()) {
    try {
        return f();
    } catch (ChannelException&) {
        throw;
    } catch (::jlang::Exception& e) {
        throw ChannelException(e);
    }
}
int rawFd(::jlang::SelectableChannel* ch) {
    int fd = ch->isOpen() ? ch->fd() : -1;
    if (fd < 0) throw ChannelException(::jlang::SocketException("Socket is closed"));
    return fd;
}
void setRawIntOpt(::jlang::SelectableChannel* ch, int level, int name, int value) {
    if (::setsockopt(rawFd(ch), level, name, &value, sizeof value) < 0) {
        throw ChannelException(::jlang::SocketException(String(std::strerror(errno))));
    }
}
int getRawIntOpt(::jlang::SelectableChannel* ch, int level, int name) {
    int value = 0;
    socklen_t len = sizeof value;
    if (::getsockopt(rawFd(ch), level, name, &value, &len) < 0) {
        throw ChannelException(::jlang::SocketException(String(std::strerror(errno))));
    }
    return value;
}
}  // namespace

bool SocketChannelConfig::setOption(const String& key, Object* value) {
    if (ChannelConfig::setOption(key, value)) return true;
    if (key.equals("receiveBufferSize")) {
        setReceiveBufferSize(detail::toInt(value));
    } else if (key.equals("sendBufferSize")) {
        setSendBufferSize(detail::toInt(value));
    } else if (key.equals("tcpNoDelay")) {
        setTcpNoDelay(detail::toBoolean(value));
    } else if (key.equals("keepAlive")) {
        setKeepAlive(detail::toBoolean(value));
    } else if (key.equals("reuseAddress")) {
        setReuseAddress(detail::toBoolean(value));
    } else if (key.equals("soLinger")) {
        setSoLinger(detail::toInt(value));
    } else if (key.equals("trafficClass")) {
        setTrafficClass(detail::toInt(value));
    } else if (key.equals("writeBufferHighWaterMark")) {
        setWriteBufferHighWaterMark(detail::toInt(value));
    } else if (key.equals("writeBufferLowWaterMark")) {
        setWriteBufferLowWaterMark(detail::toInt(value));
    } else if (key.equals("writeSpinCount")) {
        setWriteSpinCount(detail::toInt(value));
    } else {
        return false;
    }
    return true;
}

bool SocketChannelConfig::isTcpNoDelay() {
    return socketOption([&] { return socket_->socket()->getTcpNoDelay(); });
}
void SocketChannelConfig::setTcpNoDelay(bool v) {
    socketOption([&] { socket_->socket()->setTcpNoDelay(v); });
}
bool SocketChannelConfig::isKeepAlive() {
    return socketOption([&] { return socket_->socket()->getKeepAlive(); });
}
void SocketChannelConfig::setKeepAlive(bool v) {
    socketOption([&] { socket_->socket()->setKeepAlive(v); });
}
bool SocketChannelConfig::isReuseAddress() { return getRawIntOpt(socket_, SOL_SOCKET, SO_REUSEADDR) != 0; }
void SocketChannelConfig::setReuseAddress(bool v) {
    socketOption([&] { socket_->socket()->setReuseAddress(v); });
}
int32_t SocketChannelConfig::getTrafficClass() { return getRawIntOpt(socket_, IPPROTO_IP, IP_TOS); }
void SocketChannelConfig::setTrafficClass(int32_t v) { setRawIntOpt(socket_, IPPROTO_IP, IP_TOS, v); }
int32_t SocketChannelConfig::getReceiveBufferSize() {
    return socketOption([&] { return socket_->socket()->getReceiveBufferSize(); });
}
void SocketChannelConfig::setReceiveBufferSize(int32_t v) {
    socketOption([&] { socket_->socket()->setReceiveBufferSize(v); });
}
int32_t SocketChannelConfig::getSendBufferSize() {
    return socketOption([&] { return socket_->socket()->getSendBufferSize(); });
}
void SocketChannelConfig::setSendBufferSize(int32_t v) {
    socketOption([&] { socket_->socket()->setSendBufferSize(v); });
}

int32_t SocketChannelConfig::getSoLinger() {
    struct linger l {};
    socklen_t len = sizeof l;
    if (::getsockopt(rawFd(socket_), SOL_SOCKET, SO_LINGER, &l, &len) < 0) {
        throw ChannelException(::jlang::SocketException(String(std::strerror(errno))));
    }
    return l.l_onoff ? l.l_linger : -1;
}

void SocketChannelConfig::setSoLinger(int32_t soLinger) {
    socketOption([&] {
        if (soLinger < 0) {
            socket_->socket()->setSoLinger(false, 0);
        } else {
            socket_->socket()->setSoLinger(true, soLinger);
        }
    });
}

void SocketChannelConfig::setWriteBufferHighWaterMark(int32_t v) {
    if (v < getWriteBufferLowWaterMark()) {
        throw ::jlang::IllegalArgumentException(::jlang::str(
            "writeBufferHighWaterMark cannot be less than writeBufferLowWaterMark (", getWriteBufferLowWaterMark(),
            "): ", v));
    }
    if (v < 0) throw ::jlang::IllegalArgumentException(::jlang::str("writeBufferHighWaterMark: ", v));
    writeBufferHighWaterMark_.store(v);
}

void SocketChannelConfig::setWriteBufferLowWaterMark(int32_t v) {
    if (v > getWriteBufferHighWaterMark()) {
        throw ::jlang::IllegalArgumentException(::jlang::str(
            "writeBufferLowWaterMark cannot be greater than writeBufferHighWaterMark (", getWriteBufferHighWaterMark(),
            "): ", v));
    }
    if (v < 0) throw ::jlang::IllegalArgumentException(::jlang::str("writeBufferLowWaterMark: ", v));
    writeBufferLowWaterMark_.store(v);
}

void SocketChannelConfig::setWriteSpinCount(int32_t v) {
    if (v <= 0) throw ::jlang::IllegalArgumentException("writeSpinCount must be a positive integer.");
    writeSpinCount_.store(v);
}

bool ServerSocketChannelConfig::setOption(const String& key, Object* value) {
    if (key.equals("pipelineFactory")) {
        setPipelineFactory(::jlang::cast<ChannelPipelineFactory>(value));
    } else if (key.equals("bufferFactory")) {
        setBufferFactory(::jlang::cast<ChannelBufferFactory>(value));
    } else if (key.equals("receiveBufferSize")) {
        setReceiveBufferSize(detail::toInt(value));
    } else if (key.equals("reuseAddress")) {
        setReuseAddress(detail::toBoolean(value));
    } else if (key.equals("backlog")) {
        setBacklog(detail::toInt(value));
    } else {
        return false;
    }
    return true;
}

void ServerSocketChannelConfig::setBacklog(int32_t backlog) {
    if (backlog < 0) throw ::jlang::IllegalArgumentException(::jlang::str("backlog: ", backlog));
    backlog_.store(backlog);
}
bool ServerSocketChannelConfig::isReuseAddress() {
    return socketOption([&] { return socket_->socket()->getReuseAddress(); });
}
void ServerSocketChannelConfig::setReuseAddress(bool v) {
    socketOption([&] { socket_->socket()->setReuseAddress(v); });
}
int32_t ServerSocketChannelConfig::getReceiveBufferSize() {
    return socketOption([&] { return socket_->socket()->getReceiveBufferSize(); });
}
void ServerSocketChannelConfig::setReceiveBufferSize(int32_t v) {
    socketOption([&] { socket_->socket()->setReceiveBufferSize(v); });
}

// =======================================================================================
// ChannelGroupFuture / ChannelGroup
// =======================================================================================
namespace {
class GroupChildListener final : public ChannelFutureListener {
public:
    explicit GroupChildListener(ChannelGroupFuture* f) : f_(f) {}
    void operationComplete(ChannelFuture* future) override { f_->childDone(future->isSuccess()); }

private:
    ChannelGroupFuture* f_;
};
std::atomic<int32_t> g_nextGroupId{0};
}  // namespace

ChannelGroupFuture::ChannelGroupFuture(ChannelGroup* group, const std::vector<ChannelFuture*>& futures)
    : group_(group) {
    if (group == nullptr) throw ::jlang::NullPointerException("group");
    // LinkedHashMap keyed by channel id: a later future for the same channel replaces the earlier.
    for (ChannelFuture* f : futures) {
        bool replaced = false;
        for (auto& existing : futures_) {
            if (existing->getChannel()->getId() == f->getChannel()->getId()) {
                existing = f;
                replaced = true;
                break;
            }
        }
        if (!replaced) futures_.push_back(f);
    }
    auto* childListener = new GroupChildListener(this);
    for (ChannelFuture* f : futures_) f->addListener(childListener);
    if (futures_.empty()) setDone();
}

ChannelFuture* ChannelGroupFuture::find(int32_t channelId) {
    for (ChannelFuture* f : futures_) {
        if (f->getChannel()->getId() == channelId) return f;
    }
    return nullptr;
}

ChannelFuture* ChannelGroupFuture::find(Channel* channel) { return find(channel->getId()); }

void ChannelGroupFuture::childDone(bool success) {
    bool callSetDone = false;
    JSYNC(this) {
        if (success) {
            successCount_++;
        } else {
            failureCount_++;
        }
        callSetDone = successCount_ + failureCount_ == static_cast<int32_t>(futures_.size());
    }
    if (callSetDone) setDone();
}

bool ChannelGroupFuture::isDone() {
    JSYNC(this) { return done_; }
}
bool ChannelGroupFuture::isCompleteSuccess() {
    JSYNC(this) { return successCount_ == static_cast<int32_t>(futures_.size()); }
}
bool ChannelGroupFuture::isPartialSuccess() {
    JSYNC(this) { return !futures_.empty() && successCount_ != 0; }
}
bool ChannelGroupFuture::isPartialFailure() {
    JSYNC(this) { return !futures_.empty() && failureCount_ != 0; }
}
bool ChannelGroupFuture::isCompleteFailure() {
    JSYNC(this) { return failureCount_ == static_cast<int32_t>(futures_.size()); }
}

void ChannelGroupFuture::addListener(ChannelGroupFutureListener* listener) {
    if (listener == nullptr) throw ::jlang::NullPointerException("listener");
    bool notifyNow = false;
    JSYNC(this) {
        if (done_) {
            notifyNow = true;
        } else if (firstListener_ == nullptr) {
            firstListener_ = listener;
        } else {
            otherListeners_.push_back(listener);
        }
    }
    if (notifyNow) notifyListener(listener);
}

void ChannelGroupFuture::removeListener(ChannelGroupFutureListener* listener) {
    if (listener == nullptr) throw ::jlang::NullPointerException("listener");
    JSYNC(this) {
        if (!done_) {
            if (listener == firstListener_) {
                if (!otherListeners_.empty()) {
                    firstListener_ = otherListeners_.front();
                    otherListeners_.erase(otherListeners_.begin());
                } else {
                    firstListener_ = nullptr;
                }
            } else {
                for (auto it = otherListeners_.begin(); it != otherListeners_.end(); ++it) {
                    if (*it == listener) {
                        otherListeners_.erase(it);
                        break;
                    }
                }
            }
        }
    }
}

ChannelGroupFuture* ChannelGroupFuture::await() {
    if (currentThreadInterrupted()) throw ::jlang::InterruptedException();
    JSYNC(this) {
        while (!done_) {
            if (DefaultChannelFuture::isUseDeadLockChecker() && detail::inIoThread()) {
                throw ::jlang::IllegalStateException(
                    "await*() in I/O thread causes a dead lock or sudden performance drop. Use addListener() "
                    "instead or call await*() from a different thread.");
            }
            waiters_++;
            try {
                wait();
            } catch (...) {
                waiters_--;
                throw;
            }
            waiters_--;
        }
    }
    return this;
}

ChannelGroupFuture* ChannelGroupFuture::awaitUninterruptibly() {
    bool interrupted = false;
    JSYNC(this) {
        while (!done_) {
            if (DefaultChannelFuture::isUseDeadLockChecker() && detail::inIoThread()) {
                throw ::jlang::IllegalStateException(
                    "await*() in I/O thread causes a dead lock or sudden performance drop. Use addListener() "
                    "instead or call await*() from a different thread.");
            }
            waiters_++;
            try {
                wait();
            } catch (::jlang::InterruptedException&) {
                interrupted = true;
            }
            waiters_--;
        }
    }
    if (interrupted) currentThreadInterrupt();
    return this;
}

bool ChannelGroupFuture::await(int64_t timeout, ::jlang::TimeUnit unit) { return await0(unit.toNanos(timeout), true); }
bool ChannelGroupFuture::await(int64_t timeoutMillis) { return await0(timeoutMillis * 1000000, true); }
bool ChannelGroupFuture::awaitUninterruptibly(int64_t timeout, ::jlang::TimeUnit unit) {
    return await0(unit.toNanos(timeout), false);
}
bool ChannelGroupFuture::awaitUninterruptibly(int64_t timeoutMillis) { return await0(timeoutMillis * 1000000, false); }

bool ChannelGroupFuture::await0(int64_t timeoutNanos, bool interruptable) {
    if (interruptable && currentThreadInterrupted()) throw ::jlang::InterruptedException();
    int64_t startTime = timeoutNanos <= 0 ? 0 : detail::nanoTime();
    int64_t waitTime = timeoutNanos;
    bool interrupted = false;
    bool result = false;
    JSYNC(this) {
        if (done_ || waitTime <= 0) {
            result = done_;
        } else {
            waiters_++;
            for (;;) {
                try {
                    wait(waitTime / 1000000, static_cast<int32_t>(waitTime % 1000000));
                } catch (::jlang::InterruptedException&) {
                    if (interruptable) {
                        waiters_--;
                        throw;
                    }
                    interrupted = true;
                }
                if (done_) {
                    result = true;
                    break;
                }
                waitTime = timeoutNanos - (detail::nanoTime() - startTime);
                if (waitTime <= 0) {
                    result = done_;
                    break;
                }
            }
            waiters_--;
        }
    }
    if (interrupted) currentThreadInterrupt();
    return result;
}

void ChannelGroupFuture::setDone() {
    ChannelGroupFutureListener* first = nullptr;
    std::vector<ChannelGroupFutureListener*> others;
    JSYNC(this) {
        if (done_) return;
        done_ = true;
        if (waiters_ > 0) notifyAll();
        first = firstListener_;
        firstListener_ = nullptr;
        others.swap(otherListeners_);
    }
    if (first != nullptr) {
        notifyListener(first);
        for (auto* l : others) notifyListener(l);
    }
}

void ChannelGroupFuture::notifyListener(ChannelGroupFutureListener* l) {
    try {
        l->operationComplete(this);
    } catch (Throwable& t) {
        detail::logWarn("org.jboss.netty.channel.group.DefaultChannelGroupFuture",
                        "An exception was thrown by ChannelGroupFutureListener.", &t);
    }
}

ChannelGroup::ChannelGroup() : ChannelGroup(::jlang::str("group-0x", ::jlang::Integer::toHexString(++g_nextGroupId))) {}

ChannelGroup::ChannelGroup(const String& name) : name_(name) {
    if (name.isNull()) throw ::jlang::NullPointerException("name");
    remover_ = ChannelFutureListener::of([this](ChannelFuture* future) { remove(future->getChannel()); });
}

bool ChannelGroup::isEmpty() {
    std::lock_guard<std::mutex> g(lock_);
    return serverChannels_.empty() && nonServerChannels_.empty();
}

int32_t ChannelGroup::size() {
    std::lock_guard<std::mutex> g(lock_);
    return static_cast<int32_t>(serverChannels_.size() + nonServerChannels_.size());
}

Channel* ChannelGroup::find(int32_t id) {
    std::lock_guard<std::mutex> g(lock_);
    auto it = nonServerChannels_.find(id);
    if (it != nonServerChannels_.end()) return it->second;
    it = serverChannels_.find(id);
    return it == serverChannels_.end() ? nullptr : it->second;
}

bool ChannelGroup::contains(Channel* c) {
    if (c == nullptr) return false;
    std::lock_guard<std::mutex> g(lock_);
    if (dynamic_cast<ServerChannel*>(c) != nullptr) return serverChannels_.count(c->getId()) != 0;
    return nonServerChannels_.count(c->getId()) != 0;
}

bool ChannelGroup::add(Channel* channel) {
    bool added;
    {
        std::lock_guard<std::mutex> g(lock_);
        auto& map = dynamic_cast<ServerChannel*>(channel) != nullptr ? serverChannels_ : nonServerChannels_;
        added = map.emplace(channel->getId(), channel).second;
    }
    if (added) channel->getCloseFuture()->addListener(remover_);
    return added;
}

bool ChannelGroup::remove(Channel* c) {
    if (c == nullptr) return false;
    Channel* removed = nullptr;
    {
        std::lock_guard<std::mutex> g(lock_);
        auto& map = dynamic_cast<ServerChannel*>(c) != nullptr ? serverChannels_ : nonServerChannels_;
        auto it = map.find(c->getId());
        if (it != map.end()) {
            removed = it->second;
            map.erase(it);
        }
    }
    if (removed == nullptr) return false;
    removed->getCloseFuture()->removeListener(remover_);
    return true;
}

void ChannelGroup::clear() {
    std::lock_guard<std::mutex> g(lock_);
    nonServerChannels_.clear();
    serverChannels_.clear();
}

std::vector<Channel*> ChannelGroup::toVector() {
    std::lock_guard<std::mutex> g(lock_);
    std::vector<Channel*> v;
    for (auto& e : serverChannels_) v.push_back(e.second);
    for (auto& e : nonServerChannels_) v.push_back(e.second);
    return v;
}

template<class Op>
ChannelGroupFuture* ChannelGroup::forAll(Op op) {
    std::vector<Channel*> servers, others;
    {
        std::lock_guard<std::mutex> g(lock_);
        for (auto& e : serverChannels_) servers.push_back(e.second);
        for (auto& e : nonServerChannels_) others.push_back(e.second);
    }
    std::vector<ChannelFuture*> futures;
    for (Channel* c : servers) futures.push_back(op(c)->awaitUninterruptibly());
    for (Channel* c : others) futures.push_back(op(c));
    return new ChannelGroupFuture(this, futures);
}

ChannelGroupFuture* ChannelGroup::close() {
    return forAll([](Channel* c) { return c->close(); });
}
ChannelGroupFuture* ChannelGroup::disconnect() {
    return forAll([](Channel* c) { return c->disconnect(); });
}
ChannelGroupFuture* ChannelGroup::unbind() {
    return forAll([](Channel* c) { return c->unbind(); });
}
ChannelGroupFuture* ChannelGroup::setInterestOps(int32_t interestOps) {
    return forAll([interestOps](Channel* c) { return c->setInterestOps(interestOps); });
}
ChannelGroupFuture* ChannelGroup::setReadable(bool readable) {
    return forAll([readable](Channel* c) { return c->setReadable(readable); });
}

ChannelGroupFuture* ChannelGroup::write(Object* message) {
    std::vector<ChannelFuture*> futures;
    auto* buf = dynamic_cast<ChannelBuffer*>(message);
    for (Channel* c : toVector()) futures.push_back(c->write(buf != nullptr ? buf->duplicate() : message));
    return new ChannelGroupFuture(this, futures);
}

int32_t ChannelGroup::compareTo(ChannelGroup* o) {
    int32_t v = getName().compareTo(o->getName());
    if (v != 0) return v;
    return identityHashCode() - o->identityHashCode();
}

String ChannelGroup::toString() { return ::jlang::str("DefaultChannelGroup(name: ", getName(), ", size: ", size(), ")"); }

// =======================================================================================
// Bootstrap / ServerBootstrap
// =======================================================================================

Bootstrap::Bootstrap() {
    pipeline_ = Channels::pipeline();
    pipelineFactory_ = Channels::pipelineFactory(pipeline_);
}

Bootstrap::Bootstrap(ChannelFactory* channelFactory) : Bootstrap() { Bootstrap::setFactory(channelFactory); }

ChannelFactory* Bootstrap::getFactory() {
    std::lock_guard<std::mutex> g(lock_);
    if (factory_ == nullptr) throw ::jlang::IllegalStateException("factory is not set yet.");
    return factory_;
}

void Bootstrap::setFactory(ChannelFactory* factory) {
    if (factory == nullptr) throw ::jlang::NullPointerException("factory");
    std::lock_guard<std::mutex> g(lock_);
    if (factory_ != nullptr) throw ::jlang::IllegalStateException("factory can't change once set.");
    factory_ = factory;
}

ChannelPipeline* Bootstrap::getPipeline() {
    std::lock_guard<std::mutex> g(lock_);
    if (pipeline_ == nullptr) {
        throw ::jlang::IllegalStateException("getPipeline() cannot be called if setPipelineFactory() was called.");
    }
    return pipeline_;
}

void Bootstrap::setPipeline(ChannelPipeline* pipeline) {
    if (pipeline == nullptr) throw ::jlang::NullPointerException("pipeline");
    std::lock_guard<std::mutex> g(lock_);
    pipeline_ = pipeline;
    pipelineFactory_ = Channels::pipelineFactory(pipeline);
}

ChannelPipelineFactory* Bootstrap::getPipelineFactory() {
    std::lock_guard<std::mutex> g(lock_);
    return pipelineFactory_;
}

void Bootstrap::setPipelineFactory(ChannelPipelineFactory* pipelineFactory) {
    if (pipelineFactory == nullptr) throw ::jlang::NullPointerException("pipelineFactory");
    std::lock_guard<std::mutex> g(lock_);
    pipeline_ = nullptr;
    pipelineFactory_ = pipelineFactory;
}

std::map<String, Object*> Bootstrap::getOptions() {
    std::lock_guard<std::mutex> g(lock_);
    return options_;
}

void Bootstrap::setOptions(const std::map<String, Object*>& options) {
    std::lock_guard<std::mutex> g(lock_);
    options_ = options;
}

Object* Bootstrap::getOption(const String& key) {
    if (key.isNull()) throw ::jlang::NullPointerException("key");
    std::lock_guard<std::mutex> g(lock_);
    auto it = options_.find(key);
    return it == options_.end() ? nullptr : it->second;
}

void Bootstrap::setOption(const String& key, Object* value) {
    if (key.isNull()) throw ::jlang::NullPointerException("key");
    std::lock_guard<std::mutex> g(lock_);
    if (value == nullptr) {
        options_.erase(key);
    } else {
        options_[key] = value;
    }
}

void Bootstrap::setOption(const String& key, bool value) { setOption(key, detail::boxBool(value)); }
void Bootstrap::setOption(const String& key, int32_t value) { setOption(key, detail::boxInt(value)); }
void Bootstrap::setOption(const String& key, int64_t value) {
    setOption(key, static_cast<Object*>(::jlang::Long::valueOf(value)));
}

void Bootstrap::releaseExternalResources() {
    ChannelFactory* f;
    {
        std::lock_guard<std::mutex> g(lock_);
        f = factory_;
    }
    if (f != nullptr) f->releaseExternalResources();
}

namespace {

// A tiny blocking queue of futures (LinkedBlockingQueue<ChannelFuture>).
class FutureQueue final : public virtual Object {
public:
    void offer(ChannelFuture* f) {
        {
            std::lock_guard<std::mutex> g(lock_);
            q_.push_back(f);
        }
        cond_.notify_all();
    }
    ChannelFuture* take() {
        std::unique_lock<std::mutex> g(lock_);
        cond_.wait(g, [this] { return !q_.empty(); });
        ChannelFuture* f = q_.front();
        q_.pop_front();
        return f;
    }

private:
    std::mutex lock_;
    std::condition_variable cond_;
    std::deque<ChannelFuture*> q_;
};

// ServerBootstrap.Binder
class Binder final : public SimpleChannelUpstreamHandler {
public:
    Binder(ServerBootstrap* bootstrap, InetSocketAddress* localAddress, FutureQueue* futureQueue)
        : bootstrap_(bootstrap), localAddress_(localAddress), futureQueue_(futureQueue) {}

    void channelOpen(ChannelHandlerContext* ctx, ChannelStateEvent* evt) override {
        try {
            evt->getChannel()->getConfig()->setPipelineFactory(bootstrap_->getPipelineFactory());
            std::map<String, Object*> allOptions = bootstrap_->getOptions();
            std::map<String, Object*> parentOptions;
            for (auto& e : allOptions) {
                if (e.first.startsWith("child.")) {
                    childOptions_[e.first.substring(6)] = e.second;
                } else if (!e.first.equals("pipelineFactory")) {
                    parentOptions[e.first] = e.second;
                }
            }
            evt->getChannel()->getConfig()->setOptions(parentOptions);
        } catch (...) {
            ctx->sendUpstream(evt);
            throw;
        }
        ctx->sendUpstream(evt);
        futureQueue_->offer(evt->getChannel()->bind(localAddress_));
    }

    void childChannelOpen(ChannelHandlerContext* ctx, ChildChannelStateEvent* e) override {
        e->getChildChannel()->getConfig()->setOptions(childOptions_);
        ctx->sendUpstream(e);
    }

    void exceptionCaught(ChannelHandlerContext* ctx, ExceptionEvent* e) override {
        futureQueue_->offer(Channels::failedFuture(e->getChannel(), e->getCause()));
        ctx->sendUpstream(e);
    }

private:
    ServerBootstrap* bootstrap_;
    InetSocketAddress* localAddress_;
    FutureQueue* futureQueue_;
    std::map<String, Object*> childOptions_;
};

}  // namespace

ServerBootstrap::ServerBootstrap(ChannelFactory* channelFactory) { setFactory(channelFactory); }

void ServerBootstrap::setFactory(ChannelFactory* factory) {
    if (factory == nullptr) throw ::jlang::NullPointerException("factory");
    if (dynamic_cast<ServerChannelFactory*>(factory) == nullptr) {
        throw ::jlang::IllegalArgumentException(
            ::jlang::str("factory must be a ServerChannelFactory: ", factory->getClass()));
    }
    Bootstrap::setFactory(factory);
}

Channel* ServerBootstrap::bind() {
    auto* localAddress = dynamic_cast<InetSocketAddress*>(getOption("localAddress"));
    if (localAddress == nullptr) throw ::jlang::IllegalStateException("localAddress option is not set.");
    return bind(localAddress);
}

Channel* ServerBootstrap::bind(InetSocketAddress* localAddress) {
    if (localAddress == nullptr) throw ::jlang::NullPointerException("localAddress");
    auto* futureQueue = new FutureQueue();
    ChannelHandler* binder = new Binder(this, localAddress, futureQueue);
    ChannelHandler* parentHandler = getParentHandler();
    ChannelPipeline* bossPipeline = Channels::pipeline();
    bossPipeline->addLast("binder", binder);
    if (parentHandler != nullptr) bossPipeline->addLast("userHandler", parentHandler);

    Channel* channel = getFactory()->newChannel(bossPipeline);
    ChannelFuture* future = futureQueue->take();
    future->awaitUninterruptibly();
    if (!future->isSuccess()) {
        future->getChannel()->close()->awaitUninterruptibly();
        throw ChannelException(::jlang::str("Failed to bind to: ", localAddress), future->getCause());
    }
    return channel;
}

// =======================================================================================
// FrameDecoder / LengthFieldBasedFrameDecoder
// =======================================================================================

void FrameDecoder::messageReceived(ChannelHandlerContext* ctx, MessageEvent* e) {
    Object* m = e->getMessage();
    auto* input = dynamic_cast<ChannelBuffer*>(m);
    if (input == nullptr) {
        ctx->sendUpstream(e);
        return;
    }
    if (!input->readable()) return;

    std::lock_guard<std::recursive_mutex> decodeGuard(decodeLock_);
    ChannelBuffer* cumulation = this->cumulation(ctx);
    if (cumulation->readable()) {
        cumulation->discardReadBytes();
        cumulation->writeBytes(input);
        callDecode(ctx, e->getChannel(), cumulation, e->getRemoteAddress());
    } else {
        callDecode(ctx, e->getChannel(), input, e->getRemoteAddress());
        if (input->readable()) cumulation->writeBytes(input);
    }
}

void FrameDecoder::channelDisconnected(ChannelHandlerContext* ctx, ChannelStateEvent* e) { cleanup(ctx, e); }
void FrameDecoder::channelClosed(ChannelHandlerContext* ctx, ChannelStateEvent* e) { cleanup(ctx, e); }
void FrameDecoder::exceptionCaught(ChannelHandlerContext* ctx, ExceptionEvent* e) { ctx->sendUpstream(e); }

Object* FrameDecoder::decodeLast(ChannelHandlerContext* ctx, Channel* channel, ChannelBuffer* buffer) {
    return decode(ctx, channel, buffer);
}

void FrameDecoder::callDecode(ChannelHandlerContext* context, Channel* channel, ChannelBuffer* cumulation,
                              InetSocketAddress* remoteAddress) {
    while (cumulation->readable()) {
        int32_t oldReaderIndex = cumulation->readerIndex();
        Object* frame = decode(context, channel, cumulation);
        if (frame == nullptr) {
            if (oldReaderIndex == cumulation->readerIndex()) break;  // more data is required
            continue;                                                // previous data was discarded
        } else if (oldReaderIndex == cumulation->readerIndex()) {
            throw ::jlang::IllegalStateException(::jlang::str(
                "decode() method must read at least one byte if it returned a frame (caused by: ", getClass(), ")"));
        }
        unfoldAndFireMessageReceived(context, remoteAddress, frame);
    }
}

void FrameDecoder::unfoldAndFireMessageReceived(ChannelHandlerContext* context, InetSocketAddress* remoteAddress,
                                                Object* result) {
    if (unfold_) {
        if (auto* arr = dynamic_cast<::jlang::Array<Object*>*>(result)) {
            for (Object* r : *arr) Channels::fireMessageReceived(context, r, remoteAddress);
            return;
        }
    }
    Channels::fireMessageReceived(context, result, remoteAddress);
}

void FrameDecoder::cleanup(ChannelHandlerContext* ctx, ChannelStateEvent* e) {
    std::unique_lock<std::recursive_mutex> decodeGuard(decodeLock_, std::try_to_lock);
    if (!decodeGuard.owns_lock()) {
        // Another thread (the I/O thread) is decoding right now; it delivers what it has.
        ctx->sendUpstream(e);
        return;
    }
    ChannelBuffer* cumulation = cumulation_;
    if (cumulation == nullptr) {
        ctx->sendUpstream(e);
        return;
    }
    cumulation_ = nullptr;
    try {
        if (cumulation->readable()) callDecode(ctx, ctx->getChannel(), cumulation, nullptr);
        // decodeLast() is called even if there's nothing more to read from the buffer.
        Object* partialFrame = decodeLast(ctx, ctx->getChannel(), cumulation);
        if (partialFrame != nullptr) unfoldAndFireMessageReceived(ctx, nullptr, partialFrame);
    } catch (...) {
        ctx->sendUpstream(e);  // finally
        throw;
    }
    ctx->sendUpstream(e);
}

ChannelBuffer* FrameDecoder::cumulation(ChannelHandlerContext* ctx) {
    ChannelBuffer* c = cumulation_;
    if (c == nullptr) {
        c = ChannelBuffers::dynamicBuffer(ctx->getChannel()->getConfig()->getBufferFactory());
        cumulation_ = c;
    }
    return c;
}

LengthFieldBasedFrameDecoder::LengthFieldBasedFrameDecoder(int32_t maxFrameLength, int32_t lengthFieldOffset,
                                                           int32_t lengthFieldLength)
    : LengthFieldBasedFrameDecoder(maxFrameLength, lengthFieldOffset, lengthFieldLength, 0, 0) {}

LengthFieldBasedFrameDecoder::LengthFieldBasedFrameDecoder(int32_t maxFrameLength, int32_t lengthFieldOffset,
                                                           int32_t lengthFieldLength, int32_t lengthAdjustment,
                                                           int32_t initialBytesToStrip) {
    if (maxFrameLength <= 0) {
        throw ::jlang::IllegalArgumentException(
            ::jlang::str("maxFrameLength must be a positive integer: ", maxFrameLength));
    }
    if (lengthFieldOffset < 0) {
        throw ::jlang::IllegalArgumentException(
            ::jlang::str("lengthFieldOffset must be a non-negative integer: ", lengthFieldOffset));
    }
    if (initialBytesToStrip < 0) {
        throw ::jlang::IllegalArgumentException(
            ::jlang::str("initialBytesToStrip must be a non-negative integer: ", initialBytesToStrip));
    }
    if (lengthFieldLength != 1 && lengthFieldLength != 2 && lengthFieldLength != 3 && lengthFieldLength != 4 &&
        lengthFieldLength != 8) {
        throw ::jlang::IllegalArgumentException(
            ::jlang::str("lengthFieldLength must be either 1, 2, 3, 4, or 8: ", lengthFieldLength));
    }
    if (lengthFieldOffset > maxFrameLength - lengthFieldLength) {
        throw ::jlang::IllegalArgumentException(::jlang::str(
            "maxFrameLength (", maxFrameLength, ") must be equal to or greater than lengthFieldOffset (",
            lengthFieldOffset, ") + lengthFieldLength (", lengthFieldLength, ")."));
    }
    maxFrameLength_ = maxFrameLength;
    lengthFieldOffset_ = lengthFieldOffset;
    lengthFieldLength_ = lengthFieldLength;
    lengthAdjustment_ = lengthAdjustment;
    lengthFieldEndOffset_ = lengthFieldOffset + lengthFieldLength;
    initialBytesToStrip_ = initialBytesToStrip;
}

Object* LengthFieldBasedFrameDecoder::decode(ChannelHandlerContext*, Channel*, ChannelBuffer* buffer) {
    if (discardingTooLongFrame_) {
        int64_t bytesToDiscard = bytesToDiscard_;
        int32_t localBytesToDiscard = static_cast<int32_t>(std::min<int64_t>(bytesToDiscard, buffer->readableBytes()));
        buffer->skipBytes(localBytesToDiscard);
        bytesToDiscard -= localBytesToDiscard;
        bytesToDiscard_ = bytesToDiscard;
        if (bytesToDiscard == 0) {
            // Reset to the initial state and tell the handlers that the frame was too large.
            discardingTooLongFrame_ = false;
            int64_t tooLongFrameLength = tooLongFrameLength_;
            tooLongFrameLength_ = 0;
            throw TooLongFrameException(
                ::jlang::str("Adjusted frame length exceeds ", maxFrameLength_, ": ", tooLongFrameLength));
        }
        return nullptr;  // keep discarding
    }

    if (buffer->readableBytes() < lengthFieldEndOffset_) return nullptr;

    int32_t actualLengthFieldOffset = buffer->readerIndex() + lengthFieldOffset_;
    int64_t frameLength;
    switch (lengthFieldLength_) {
    case 1: frameLength = buffer->getUnsignedByte(actualLengthFieldOffset); break;
    case 2: frameLength = buffer->getUnsignedShort(actualLengthFieldOffset); break;
    case 3: frameLength = buffer->getUnsignedMedium(actualLengthFieldOffset); break;
    case 4: frameLength = buffer->getUnsignedInt(actualLengthFieldOffset); break;
    case 8: frameLength = buffer->getLong(actualLengthFieldOffset); break;
    default: throw ::jlang::Error("should not reach here");
    }

    if (frameLength < 0) {
        buffer->skipBytes(lengthFieldEndOffset_);
        throw CorruptedFrameException(::jlang::str("negative pre-adjustment length field: ", frameLength));
    }

    frameLength += lengthAdjustment_ + lengthFieldEndOffset_;
    if (frameLength < lengthFieldEndOffset_) {
        buffer->skipBytes(lengthFieldEndOffset_);
        throw CorruptedFrameException(::jlang::str("Adjusted frame length (", frameLength,
                                                   ") is less than lengthFieldEndOffset: ", lengthFieldEndOffset_));
    }

    if (frameLength > maxFrameLength_) {
        // Enter the discard mode and discard everything received so far.
        discardingTooLongFrame_ = true;
        tooLongFrameLength_ = frameLength;
        bytesToDiscard_ = frameLength - buffer->readableBytes();
        buffer->skipBytes(buffer->readableBytes());
        return nullptr;
    }

    // never overflows because it's less than maxFrameLength
    int32_t frameLengthInt = static_cast<int32_t>(frameLength);
    if (buffer->readableBytes() < frameLengthInt) return nullptr;

    if (initialBytesToStrip_ > frameLengthInt) {
        buffer->skipBytes(frameLengthInt);
        throw CorruptedFrameException(::jlang::str("Adjusted frame length (", frameLength,
                                                   ") is less than initialBytesToStrip: ", initialBytesToStrip_));
    }
    buffer->skipBytes(initialBytesToStrip_);
    return buffer->readBytes(frameLengthInt - initialBytesToStrip_);
}

// =======================================================================================
// OneToOneDecoder / OneToOneEncoder
// =======================================================================================

void OneToOneDecoder::handleUpstream(ChannelHandlerContext* ctx, ChannelEvent* evt) {
    auto* e = dynamic_cast<MessageEvent*>(evt);
    if (e == nullptr) {
        ctx->sendUpstream(evt);
        return;
    }
    Object* originalMessage = e->getMessage();
    Object* decodedMessage = decode(ctx, e->getChannel(), originalMessage);
    if (originalMessage == decodedMessage) {
        ctx->sendUpstream(evt);
    } else if (decodedMessage != nullptr) {
        Channels::fireMessageReceived(ctx, decodedMessage, e->getRemoteAddress());
    }
}

void OneToOneEncoder::handleDownstream(ChannelHandlerContext* ctx, ChannelEvent* evt) {
    auto* e = dynamic_cast<MessageEvent*>(evt);
    if (e == nullptr) {
        ctx->sendDownstream(evt);
        return;
    }
    Object* originalMessage = e->getMessage();
    Object* encodedMessage = encode(ctx, e->getChannel(), originalMessage);
    if (originalMessage == encodedMessage) {
        ctx->sendDownstream(evt);
    } else if (encodedMessage != nullptr) {
        Channels::write(ctx, e->getFuture(), encodedMessage, e->getRemoteAddress());
    }
}

}  // namespace jlang::netty
