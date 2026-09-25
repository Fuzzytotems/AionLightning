// jlang/fwd.h - forward declarations of every jlang class that is NOT part of the core
// headers (tools/cppgen/jdkmap.tsv rows whose header is not "core"), so generated headers can
// declare pointers to them without including the service headers. Include the named header
// (<jlang/Thread.h>, <jlang/IO.h>, ...) where the full type is needed.
//
// Generated from tools/cppgen/jdkmap.tsv; keep in sync when rows are added.
// Value types (TimeUnit, ByteOrder) are declared too: a header holding one by value must
// include its real header.
#pragma once

namespace jlang {
// <jlang/Thread.h>
class Thread;
class ThreadGroup;
class Thread_UncaughtExceptionHandler;
template<class T> class BlockingQueue;
template<class T> class LinkedBlockingQueue;
template<class T> class ArrayBlockingQueue;
template<class T> class SynchronousQueue;
template<class T> class PriorityBlockingQueue;
class Timer;
class TimerTask;
template<class T> class Callable;
class Future;
class ScheduledFuture;
class FutureTask;
class Delayed;
class Executor;
class ExecutorService;
class ScheduledExecutorService;
class AbstractExecutorService;
class ThreadPoolExecutor;
class ScheduledThreadPoolExecutor;
class ThreadFactory;
class RejectedExecutionHandler;
class TimeUnit;  // value type
class CountDownLatch;
class AtomicInteger;
class AtomicLong;
class AtomicBoolean;
template<class T> class AtomicReference;
class Lock;
class ReentrantLock;
class ReentrantReadWriteLock;
class Condition;

// <jlang/Ref.h>
template<class T> class Reference;
template<class T> class SoftReference;
template<class T> class WeakReference;
template<class T> class ReferenceQueue;

// <jlang/IO.h>
class Properties;
class ResourceBundle;
class ZipOutputStream;
class ZipEntry;
class CRC32;
class File;
class FileFilter;
class FilenameFilter;
class InputStream;
class FileInputStream;
class BufferedInputStream;
class ByteArrayInputStream;
class DataInputStream;
class OutputStream;
class FileOutputStream;
class BufferedOutputStream;
class ByteArrayOutputStream;
class DataOutputStream;
class PrintStream;  // defined in the core (<jlang/System.h>); IO.h extends it
class Reader;
class InputStreamReader;
class FileReader;
class BufferedReader;
class Writer;
class OutputStreamWriter;
class FileWriter;
class BufferedWriter;
class PrintWriter;
class LineIterator;

// <jlang/Time.h>
class Date;
class Calendar;
class GregorianCalendar;
class TimeZone;
class Locale;
class Timestamp;
class SimpleDateFormat;
class DecimalFormat;

// <jlang/Regex.h>
class Pattern;
class Matcher;

// <jlang/Nio.h>
class ByteBuffer;
class CharBuffer;
class ByteOrder;  // value type
class Charset;
class SelectableChannel;
class ServerSocketChannel;
class SocketChannel;
class Selector;
class SelectorProvider;
class SelectionKey;
class InetAddress;
class InetSocketAddress;
class Socket;
class ServerSocket;

// <jlang/Crypto.h>
class BigInteger;
class MessageDigest;
class SecureRandom;
class KeyPairGenerator;
class KeyPair;
class Key;
class RSAPublicKey;
class RSAPrivateKey;
class RSAKey;
class RSAKeyGenParameterSpec;
class Cipher;
class KeyGenerator;
class SecretKey;

// <jlang/Sql.h>
class Connection;
class Statement;
class PreparedStatement;
class CallableStatement;
class ResultSet;
class ResultSetMetaData;
class DatabaseMetaData;
class Savepoint;
class DataSource;

// <jlang/Geom.h>
class Point;
class Polygon;
class Rectangle;

// <jlang/Log.h>
class Logger;

}  // namespace jlang

namespace jlang::xml {
// <jlang/Xml.h>
class JAXBContext;
class Unmarshaller;
class Marshaller;
class JAXBException;
class XmlAdapter;
class Schema;
class SchemaFactory;
class QName;
class SAXParserFactory;
class SAXParser;
class DefaultHandler;
class Attributes;
class Locator;
class SAXException;
class SAXParseException;
class XMLInputFactory;
class XMLOutputFactory;
class XMLEventFactory;
class XMLEventReader;
class XMLEventWriter;
class XMLStreamException;
class XMLEvent;
class XMLAttribute;

}  // namespace jlang::xml

namespace jlang::log4j {
// <jlang/Log.h>
class Level;
class LoggerFactory;
class LoggingEvent;
class ThrowableInformation;
class Filter;
class FileAppender;
class AppenderSkeleton;

}  // namespace jlang::log4j

namespace jlang::netty {
// <jlang/Netty.h>
class ServerBootstrap;
class ChannelBuffer;
class HeapChannelBufferFactory;
class Channel;
class ChannelFactory;
class ChannelFuture;
class ChannelFutureListener;
class ChannelHandler;
class ChannelHandlerContext;
class ChannelPipeline;
class ChannelPipelineFactory;
class ChannelStateEvent;
class ExceptionEvent;
class MessageEvent;
class SimpleChannelUpstreamHandler;
class ChannelGroup;
class ChannelGroupFuture;
class NioServerSocketChannelFactory;
class LengthFieldBasedFrameDecoder;
class OneToOneDecoder;
class OneToOneEncoder;
class ExecutionHandler;
class OrderedMemoryAwareThreadPoolExecutor;

}  // namespace jlang::netty
