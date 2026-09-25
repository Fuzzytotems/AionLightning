#!/usr/bin/env python3
"""Compare two directories of .class files: the classes of geoEngine-0.1.jar (REF_DIR, e.g. the jar
unzipped) and the classes recompiled from geoengine/src (NEW_DIR, see build.sh).

usage: bcdiff.py REF_DIR NEW_DIR [--code] [--show N] [--only CLASS_SUBSTRING]

Checks, for every aionjHungary.* class (test.GeodataLoad is not part of the port):
  * API: class header (access flags, superclass, interfaces, generic Signature, named member classes)
    and every non-synthetic field/method (javap -p declaration, descriptor, access flags, generic
    Signature, throws clause, ConstantValue, Deprecated attribute, annotations).  Private and
    package-private members are included.
  * synthetic members (reported for information).
  * with --code: every method body.  javap -c output is normalized so that the output of javac 6
    (which built the jar) and of javac >= 8 for the same source compare equal: constant-pool indices
    are replaced by the symbolic constant, branch offsets by labels, non-parameter local slots are
    renumbered by first use, goto chains are threaded, 'if<c> L1; goto L2; L1:' becomes 'if<!c> L2',
    enum switches (javac 6 switch-map arrays vs. javac 21 direct ordinal() switches) are rewritten to
    switches over constant names, enum $values() is inlined, adjacent string-literal appends are
    merged, duplicate checkcasts dropped and Object methods on interface receivers unified.  The
    normalization never changes which instructions execute or their order.  Local-variable names and
    types from the LocalVariableTable are compared too.

Differences listed in EXPECTED are explained (stale jar classes; one javac cast-insertion
difference); anything else is printed as UNEXPLAINED and the exit status is 1.
"""
import os
import re
import subprocess
import sys
import difflib

BRANCH_NEG = {
    'ifeq': 'ifne', 'ifne': 'ifeq', 'iflt': 'ifge', 'ifge': 'iflt', 'ifgt': 'ifle', 'ifle': 'ifgt',
    'if_icmpeq': 'if_icmpne', 'if_icmpne': 'if_icmpeq', 'if_icmplt': 'if_icmpge', 'if_icmpge': 'if_icmplt',
    'if_icmpgt': 'if_icmple', 'if_icmple': 'if_icmpgt', 'if_acmpeq': 'if_acmpne', 'if_acmpne': 'if_acmpeq',
    'ifnull': 'ifnonnull', 'ifnonnull': 'ifnull',
}
BRANCHES = set(BRANCH_NEG) | {'goto', 'goto_w', 'jsr', 'jsr_w'}
SWITCHES = ('tableswitch', 'lookupswitch', 'enumswitch', 'switch')
LOCAL_OPS = re.compile(r'^([ilfda])(load|store)(?:_(\d))?$')
TERMINAL = {'goto', 'goto_w', 'return', 'ireturn', 'lreturn', 'freturn', 'dreturn', 'areturn', 'athrow',
            'tableswitch', 'lookupswitch', 'enumswitch', 'switch'}


def list_classes(d):
    out = []
    for root, _, files in os.walk(d):
        for f in files:
            if f.endswith('.class'):
                rel = os.path.relpath(os.path.join(root, f), d)
                out.append(rel[:-6].replace(os.sep, '.'))
    return sorted(out)


def run_javap(d, classes):
    res = subprocess.run(['javap', '-p', '-v', '-c', '-constants', '-classpath', d] + classes,
                         stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
    text = res.stdout
    chunks = re.split(r'^Classfile ', text, flags=re.M)[1:]
    out = {}
    for ch in chunks:
        c = parse_class(ch.split('\n'))
        out[c['name']] = c
    return out


def parse_class(lines):
    c = {'members': [], 'attrs': []}
    i = 0
    # header
    while i < len(lines):
        l = lines[i]
        if l.startswith('  Compiled from') or l.startswith('  Last modified') or l.startswith('  SHA-256') or l.startswith('/') or l.startswith('  MD5'):
            i += 1
            continue
        break
    c['decl'] = lines[i].strip()
    i += 1
    m = re.search(r'(?:class|interface|enum)\s+([\w.$]+)', c['decl'])
    c['name'] = m.group(1)
    while i < len(lines) and not lines[i].startswith('Constant pool:') and lines[i] != '{':
        l = lines[i].strip()
        if l.startswith('flags:'):
            c['flags'] = l
        elif l.startswith('super_class:'):
            c['super'] = l.split('//')[-1].strip()
        i += 1
    # constant pool: collect for interfaces? skip
    while i < len(lines) and lines[i] != '{':
        i += 1
    i += 1
    cur = None
    section = None
    while i < len(lines):
        l = lines[i]
        if l == '}':
            i += 1
            break
        if re.match(r'^  \S', l):
            cur = {'decl': l.strip(), 'flags': '', 'descriptor': '', 'signature': '', 'exceptions': [],
                   'constant': '', 'code': [], 'extable': [], 'args_size': 0, 'lvt': [], 'deprecated': False,
                   'annotations': []}
            c['members'].append(cur)
            section = None
        elif re.match(r'^    \S', l):
            s = l.strip()
            section = None
            if s.startswith('descriptor:'):
                cur['descriptor'] = s.split(':', 1)[1].strip()
            elif s.startswith('flags:'):
                cur['flags'] = s
            elif s.startswith('Signature:'):
                cur['signature'] = s.split('//')[-1].strip()
            elif s.startswith('ConstantValue:'):
                cur['constant'] = s.split(':', 1)[1].strip()
            elif s.startswith('Exceptions:'):
                section = 'exc'
            elif s.startswith('Deprecated: true'):
                cur['deprecated'] = True
            elif s.startswith('RuntimeVisibleAnnotations:') or s.startswith('RuntimeInvisibleAnnotations:'):
                section = 'annot'
            elif s.startswith('Code:'):
                section = 'code'
            else:
                section = 'other'
        elif section == 'annot' and re.match(r'^\s+\d+: #\d+\(', l):
            pass  # raw constant-pool form; the decoded form follows on the next line
        elif section == 'annot' and re.match(r'^\s+\S', l) and not l.strip()[0].isdigit():
            cur['annotations'].append(l.strip())
        elif section == 'exc' and l.strip().startswith('throws'):
            cur['exceptions'].append(l.strip()[len('throws'):].strip())
        elif section in ('code', 'codeex', 'codeother', 'codelvt'):
            s = l.strip()
            if re.match(r'^      \S', l):
                # code sub-attribute header
                if s.startswith('stack='):
                    m = re.search(r'args_size=(\d+)', s)
                    cur['args_size'] = int(m.group(1))
                    section = 'code'
                elif s.startswith('Exception table:'):
                    section = 'codeex'
                elif s.startswith('LocalVariableTable:'):
                    section = 'codelvt'
                else:
                    section = 'codeother'
            elif section == 'code':
                cur['code'].append(l)
            elif section == 'codelvt':
                m = re.match(r'^\s*(\d+)\s+(\d+)\s+(\d+)\s+(\S+)\s+(\S+)\s*$', l)
                if m:
                    cur['lvt'].append((m.group(4), m.group(5)))
            elif section == 'codeex':
                m = re.match(r'^\s*(\d+)\s+(\d+)\s+(\d+)\s+(.*)$', l)
                if m:
                    cur['extable'].append((int(m.group(1)), int(m.group(2)), int(m.group(3)), m.group(4).strip()))
        i += 1
    # class attributes
    rest = lines[i:]
    sig = ''
    inner = []
    sect = None
    for l in rest:
        s = l.strip()
        if re.match(r'^\S', l):
            sect = None
            if s.startswith('Signature:'):
                sig = s.split('//')[-1].strip()
            elif s.startswith('InnerClasses:'):
                sect = 'inner'
            elif s.startswith('EnclosingMethod:'):
                c['enclosing'] = s.split('//')[-1].strip()
        elif sect == 'inner' and s:
            # e.g. "public static final #10= #9 of #2;   // Entry=class a/b/IntMap$Entry of class a/b/IntMap"
            flags = s.split('#')[0].strip()
            comment = s.split('//')[-1].strip()
            inner.append(flags + ' ' + comment)
    c['signature'] = sig
    c['inner'] = sorted(inner)
    return c


def is_synthetic(mem):
    return 'ACC_SYNTHETIC' in mem['flags']


def parse_code(mem):
    """Return list of [offset, op, arg] with arg normalized; switch arg is list of (key, target)."""
    ins = []
    lines = mem['code']
    i = 0
    while i < len(lines):
        l = lines[i]
        m = re.match(r'^\s*(\d+): (\w+)\s*(.*)$', l)
        if not m:
            i += 1
            continue
        off = int(m.group(1))
        op = m.group(2)
        rest = m.group(3)
        if op in ('tableswitch', 'lookupswitch'):
            cases = []
            i += 1
            while i < len(lines) and lines[i].strip() != '}':
                mm = re.match(r'^\s*(-?\d+|default): (\d+)', lines[i])
                if mm:
                    cases.append((mm.group(1), int(mm.group(2))))
                i += 1
            ins.append([off, op, cases])
            i += 1
            continue
        if '//' in rest:
            a, comment = rest.split('//', 1)
            a = a.strip()
            comment = comment.lstrip()
            extra = ''
            mm = re.match(r'^#\d+,\s*(\d+)', a)
            if mm:
                extra = ',' + mm.group(1)
            arg = comment + extra
        else:
            arg = rest.strip()
        if op in BRANCHES:
            arg = int(arg)
        ins.append([off, op, arg])
        i += 1
    return ins


def delete_index(ins, ext, d):
    del ins[d]

    def fix(t):
        return t - 1 if t > d else t
    for x in ins:
        if x[1] in BRANCHES:
            x[2] = fix(x[2])
        elif x[1] in SWITCHES:
            x[2] = [(kk, fix(t)) for kk, t in x[2]]
    for e in ext:
        e[0], e[1], e[2] = fix(e[0]), fix(e[1]), fix(e[2])


def enum_switches(ins, ext, ctx):
    """Rewrite enum switches (javac switch-map or direct ordinal()) into 'enumswitch' with constant names."""
    if ctx is None:
        return
    # pattern A: getstatic Holder.$SwitchMap$..; <expr>; invokevirtual E.ordinal; iaload; switch
    k = 0
    while k < len(ins):
        x = ins[k]
        if x[1] == 'getstatic' and '$SwitchMap$' in str(x[2]):
            m = re.match(r'Field (?:([\w/$]+)\.)?(\$SwitchMap\$[\w$]+):\[I', x[2])
            holder = m.group(1) or ctx['this']
            field = m.group(2)
            j = k + 1
            while j < len(ins) and not (ins[j][1] == 'invokevirtual' and str(ins[j][2]).endswith('.ordinal:()I')
                                          and j + 2 < len(ins) and ins[j + 1][1] == 'iaload' and ins[j + 2][1] in SWITCHES):
                j += 1
            if j < len(ins):
                enum = re.match(r'Method ([\w/$]+)\.ordinal', ins[j][2]).group(1)
                smap = ctx['switchmaps'].get((holder, field), {})
                sw = ins[j + 2]
                sw[2] = [(('default' if kk == 'default' else smap.get(int(kk), '?%s' % kk)), t) for kk, t in sw[2]]
                sw[1] = 'enumswitch'
                sw.append(enum)
                delete_index(ins, ext, j + 1)
                delete_index(ins, ext, k)
                continue
        k += 1
    # pattern B: invokevirtual E.ordinal; switch  (javac >= 21 for enums of the same compilation unit)
    for j in range(len(ins) - 1):
        if ins[j][1] == 'invokevirtual' and str(ins[j][2]).endswith('.ordinal:()I') and ins[j + 1][1] in ('tableswitch', 'lookupswitch'):
            enum = re.match(r'Method ([\w/$]+)\.ordinal', ins[j][2]).group(1)
            consts = ctx['enums'].get(enum)
            if consts is None:
                continue
            sw = ins[j + 1]
            sw[2] = [(('default' if kk == 'default' else consts[int(kk)]), t) for kk, t in sw[2]]
            sw[1] = 'enumswitch'
            sw.append(enum)
            # keep the ordinal() call so both forms read 'invokevirtual E.ordinal; enumswitch'
    for x in ins:
        if x[1] in SWITCHES:
            dflt = [t for kk, t in x[2] if kk == 'default'][0]
            cases = [(kk, t) for kk, t in x[2] if kk != 'default' and t != dflt]
            if x[1] == 'enumswitch':
                order = ctx['enums'].get(x[3], [])
                cases.sort(key=lambda c: (order.index(c[0]) if c[0] in order else 999, c[0]))
            else:
                cases.sort(key=lambda c: int(c[0]))
            x[2] = cases + [('default', dflt)]
            if x[1] != 'enumswitch':
                x[1] = 'switch'


def normalize(mem, ctx=None):
    ins = parse_code(mem)
    if not ins:
        return []
    # local slot canonicalization
    nparams = mem['args_size']
    ins2 = []
    for off, op, arg in ins:
        m = LOCAL_OPS.match(op)
        if m:
            if m.group(3) is not None:
                slot = int(m.group(3))
            else:
                slot = int(arg)
            ins2.append([off, m.group(1) + m.group(2), ('SLOT', slot)])
        elif op == 'iinc':
            a, b = [x.strip() for x in arg.split(',')]
            ins2.append([off, 'iinc', ('SLOT', int(a), b)])
        elif op == 'ldc_w':
            ins2.append([off, 'ldc', arg])
        else:
            ins2.append([off, op, arg])
    ins = ins2
    offs = [x[0] for x in ins]
    idx_of = {o: k for k, o in enumerate(offs)}
    # convert targets to indices
    for x in ins:
        if x[1] in BRANCHES:
            x[2] = idx_of[x[2]]
        elif x[1] in ('tableswitch', 'lookupswitch'):
            x[2] = [(k, idx_of[t]) for k, t in x[2]]
    code_end = len(ins)
    ext = []
    for f, t, h, typ in mem['extable']:
        ext.append([idx_of[f], idx_of.get(t, code_end), idx_of[h], typ])
    # the ordinal()-switch rewrite needs raw switch keys, do it first
    enum_switches(ins, ext, ctx)
    # javac 6 emitted a duplicate checkcast for '((T) x).m()' in some contexts; javac >= 7 emits
    # Object methods on an interface-typed receiver as invokeinterface (javac 6: invokevirtual Object)
    k = 0
    while k < len(ins):
        x = ins[k]
        if x[1] == 'invokeinterface':
            m = re.match(r'InterfaceMethod [\w/$]+\.(getClass:\(\)Ljava/lang/Class;|hashCode:\(\)I|toString:\(\)Ljava/lang/String;|equals:\(Ljava/lang/Object;\)Z),\d+$', x[2])
            if m:
                x[1] = 'invokevirtual'
                x[2] = 'Method java/lang/Object.' + m.group(1)
        # javac >= 9 folds adjacent string literals of a concatenation chain ("a" + "b" after a
        # non-constant operand); javac 6 appended them one by one
        APP = 'Method java/lang/StringBuilder.append:(Ljava/lang/String;)Ljava/lang/StringBuilder;'
        if (x[1] == 'ldc' and str(x[2]).startswith('String ') and k + 3 < len(ins) and ins[k + 1][1] == 'invokevirtual'
                and ins[k + 1][2] == APP and ins[k + 2][1] == 'ldc' and str(ins[k + 2][2]).startswith('String ')
                and ins[k + 3][1] == 'invokevirtual' and ins[k + 3][2] == APP):
            x[2] = x[2] + ins[k + 2][2][len('String '):]
            delete_index(ins, ext, k + 3)
            delete_index(ins, ext, k + 2)
            continue
        if x[1] == 'checkcast' and k + 1 < len(ins) and ins[k + 1][1] == 'checkcast' and ins[k + 1][2] == x[2]:
            delete_index(ins, ext, k + 1)
            continue
        k += 1
    if ctx is None:
        for x in ins:
            if x[1] in ('tableswitch', 'lookupswitch'):
                x[1] = 'switch'
    # iterate peephole passes
    changed = True
    while changed:
        changed = False
        n = len(ins)

        def final_target(k, seen=None):
            seen = set()
            while ins[k][1] == 'goto' and k not in seen:
                seen.add(k)
                k = ins[k][2]
            return k
        # jump threading
        for x in ins:
            if x[1] in BRANCHES and x[1] != 'jsr':
                ft = final_target(x[2])
                if ft != x[2]:
                    x[2] = ft
                    changed = True
            elif x[1] in SWITCHES:
                nt = [(k, final_target(t)) for k, t in x[2]]
                if nt != x[2]:
                    x[2] = nt
                    changed = True
        # targets set
        targets = set()
        for x in ins:
            if x[1] in BRANCHES:
                targets.add(x[2])
            elif x[1] in SWITCHES:
                for _, t in x[2]:
                    targets.add(t)
        for e in ext:
            targets.add(e[2])
            targets.add(e[0])
            targets.add(e[1])
        delete = None
        for k in range(n - 1):
            op = ins[k][1]
            # if<c> k+2 ; goto X  ==>  if<!c> X
            if op in BRANCH_NEG and ins[k][2] == k + 2 and ins[k + 1][1] == 'goto' and (k + 1) not in targets:
                ins[k][1] = BRANCH_NEG[op]
                ins[k][2] = ins[k + 1][2]
                delete = k + 1
                break
            # goto next
            if op == 'goto' and ins[k][2] == k + 1 and k not in targets:
                delete = k
                break
            # unreachable goto after terminal instruction
            if op in TERMINAL and ins[k + 1][1] == 'goto' and (k + 1) not in targets:
                delete = k + 1
                break
        if delete is not None:
            del ins[delete]

            def fix(t):
                return t - 1 if t > delete else t
            for x in ins:
                if x[1] in BRANCHES:
                    x[2] = fix(x[2])
                elif x[1] in SWITCHES:
                    x[2] = [(kk, fix(t)) for kk, t in x[2]]
            for e in ext:
                e[0], e[1], e[2] = fix(e[0]), fix(e[1]), fix(e[2])
            changed = True
    # slot renumbering: params keep their numbers
    slotmap = {}
    nxt = [0]

    def mapslot(s):
        if s < nparams:
            return 'p%d' % s
        if s not in slotmap:
            slotmap[s] = 'v%d' % len(slotmap)
        return slotmap[s]
    # labels
    targets = []
    for x in ins:
        if x[1] in BRANCHES:
            targets.append(x[2])
        elif x[1] in SWITCHES:
            targets.extend(t for _, t in x[2])
    for e in ext:
        targets.extend([e[0], e[1], e[2]])
    labels = {}
    for t in sorted(set(targets)):
        labels[t] = 'L%d' % len(labels)
    out = []
    for k, x in enumerate(ins):
        if k in labels:
            out.append(labels[k] + ':')
        op, arg = x[1], x[2]
        if isinstance(arg, tuple) and arg and arg[0] == 'SLOT':
            s = mapslot(arg[1])
            if op == 'iinc':
                out.append('  iinc %s %s' % (s, arg[2]))
            else:
                out.append('  %s %s' % (op, s))
        elif op in BRANCHES:
            out.append('  %s %s' % (op, labels[arg]))
        elif op in SWITCHES:
            out.append('  %s%s {%s}' % (op, (' ' + x[3]) if op == 'enumswitch' else '', ', '.join('%s:%s' % (kk, labels[t]) for kk, t in arg)))
        else:
            out.append(('  %s %s' % (op, arg)).rstrip())
    if len(ins) in labels:
        out.append(labels[len(ins)] + ':')
    for e in ext:
        out.append('  .catch %s from %s to %s using %s' % (e[3], labels[e[0]], labels[e[1]], labels[e[2]]))
    return out


def member_key(mem):
    # name + descriptor
    name = mem['decl']
    m = re.search(r'([\w$<>]+)\(', name)
    if m:
        n = m.group(1)
    elif mem['decl'].startswith('static {}'):
        n = '<clinit>'
    else:
        n = re.sub(r'\s*=.*$', '', name).rstrip(';').split()[-1]
    return n + ' ' + mem['descriptor']


def is_anonymous(name):
    return re.search(r'\$\d+$', name) is not None


def api_view(c, synthetic):
    """synthetic=False: public contract of a named class.  synthetic=True: compiler-generated members."""
    lines = []
    if not synthetic:
        lines.append('CLASS ' + c['decl'])
        lines.append('  ' + c.get('flags', ''))
        lines.append('  super ' + c.get('super', ''))
        lines.append('  signature ' + c['signature'])
        for inn in c['inner']:
            # only named member classes are part of the API; anonymous / switch-map holder
            # entries are encoded differently by javac 6 and javac 21
            if '=' in inn:
                lines.append('  inner ' + inn)
    mems = {}
    for mem in c['members']:
        if is_synthetic(mem) != synthetic:
            continue
        k = member_key(mem)
        v = [mem['decl'], mem['flags']]
        if mem['signature']:
            v.append('sig ' + mem['signature'])
        if mem['exceptions']:
            v.append('throws ' + ', '.join(mem['exceptions']))
        if mem['constant']:
            v.append('const ' + mem['constant'])
        if mem['deprecated']:
            v.append('Deprecated')
        for a in mem['annotations']:
            v.append('annotation ' + a)
        mems[k] = v
    for k in sorted(mems):
        lines.append('  ' + k)
        for v in mems[k]:
            lines.append('      ' + v)
    return lines


def build_ctx(classes):
    enums = {}
    switchmaps = {}
    for name, c in classes.items():
        internal = name.replace('.', '/')
        if 'ACC_ENUM' in c.get('flags', ''):
            enums[internal] = [member_key(m).split()[0] for m in c['members']
                               if 'ACC_ENUM' in m['flags'] and m['descriptor'] == 'L%s;' % internal]
        if any('$SwitchMap$' in m['decl'] for m in c['members']):
            for m in c['members']:
                if m['decl'].startswith('static {}'):
                    cur_field = None
                    cur_const = None
                    cur_int = None
                    for off, op, arg in parse_code(m):
                        if op == 'getstatic' and '$SwitchMap$' in arg:
                            cur_field = re.search(r'(\$SwitchMap\$[\w$]+):', arg).group(1)
                        elif op == 'getstatic':
                            cur_const = re.search(r'\.(\w+):L', arg).group(1)
                        elif op.startswith('iconst_'):
                            cur_int = int(op[len('iconst_'):].replace('m1', '-1'))
                        elif op in ('bipush', 'sipush'):
                            cur_int = int(arg)
                        elif op == 'iastore':
                            switchmaps.setdefault((internal, cur_field), {})[cur_int] = cur_const
    return {'enums': enums, 'switchmaps': switchmaps}


def normalized_members(c, ctx):
    internal = c['name'].replace('.', '/')
    lctx = dict(ctx)
    lctx['this'] = internal
    out = {}
    for m in c['members']:
        out[member_key(m)] = normalize(m, lctx)
    # javac >= 15 moves the enum $VALUES array construction into a synthetic $values(); inline it
    vk = [k for k in out if k.startswith('$values ')]
    if vk:
        body = [l for l in out[vk[0]] if l.strip() != 'areturn']
        for k in out:
            if k.startswith('<clinit>'):
                nl = []
                for l in out[k]:
                    if l.strip().startswith('invokestatic Method $values:'):
                        nl.extend(body)
                    else:
                        nl.append(l)
                out[k] = nl
        del out[vk[0]]
    return out


# Differences that are expected and explained (see geoengine/README.md).  Everything else is reported
# as UNEXPLAINED and makes the script exit with status 1.
EXPECTED = {
    ('api', 'aionjHungary.geoEngine.models.GeoNode'):
        'stale GeoNode.class does not implement Spatial.getVertexCount/getTriangleCount/setTransform; the '
        'source must declare them (they throw AbstractMethodError, as the JVM does for the jar)',
    ('only', 'aionjHungary.geoEngine.models.GeoNode', 'getVertexCount ()I'): 'see GeoNode above',
    ('only', 'aionjHungary.geoEngine.models.GeoNode', 'getTriangleCount ()I'): 'see GeoNode above',
    ('only', 'aionjHungary.geoEngine.models.GeoNode',
     'setTransform (LaionjHungary/geoEngine/math/Matrix3f;LaionjHungary/geoEngine/math/Vector3f;F)V'): 'see GeoNode above',
    ('code', 'aionjHungary.geoEngine.models.GeoNode', 'updateWorldBound ()V'):
        'jar calls the non-existent Spatial.updateWorldBound() (NoSuchMethodError at run time); source throws it explicitly',
    ('lvt', 'aionjHungary.geoEngine.models.GeoNode', 'updateWorldBound ()V'): 'unreachable remainder of the body removed',
    ('code', 'aionjHungary.geoEngine.bounding.BoundingSphere', 'intersects (LaionjHungary/geoEngine/bounding/BoundingVolume;)Z'):
        'jar calls the non-existent BoundingVolume.intersectsSphere() (NoSuchMethodError at run time); source throws it explicitly',
    ('code', 'aionjHungary.geoEngine.scene.Mesh', 'setInterleaved ()V'):
        'javac >= 8 inserts checkcast VertexBuffer on the generic IntMap.Entry<VertexBuffer>.getValue() result passed to '
        'ArrayList<VertexBuffer>.add(); javac 6 did not (the map only ever holds VertexBuffers)',
}


def main():
    args = sys.argv[1:]
    code = '--code' in args
    show = 10 ** 9
    only = None
    if '--show' in args:
        show = int(args[args.index('--show') + 1])
    if '--only' in args:
        only = args[args.index('--only') + 1]
    pos = []
    skip = False
    for a in args:
        if skip:
            skip = False
            continue
        if a in ('--show', '--only'):
            skip = True
            continue
        if a.startswith('--'):
            continue
        pos.append(a)
    ref, new = pos[0], pos[1]
    rc = [x for x in list_classes(ref) if x.startswith('aionjHungary.')]
    nc = [x for x in list_classes(new) if x.startswith('aionjHungary.')]
    R = run_javap(ref, rc)
    N = run_javap(new, nc)
    rctx = build_ctx(R)
    nctx = build_ctx(N)
    if only:
        R = {k: v for k, v in R.items() if only in k}
        N = {k: v for k, v in N.items() if only in k}
    missing = sorted(set(R) - set(N))
    extra = sorted(set(N) - set(R))
    api_diffs = 0
    api_names = []
    missing_real = []
    syn_diffs = 0
    code_diff_methods = []
    code_only = []
    lvt_diffs = []
    code_same = 0
    for x in missing:
        switchmap_only = all('$SwitchMap$' in m['decl'] or m['decl'].startswith('static {}') for m in R[x]['members'])
        if switchmap_only and is_anonymous(x):
            print('missing synthetic switch-map holder in new (javac 21 switches on ordinal() for same-file enums):', x)
            syn_diffs += 1
        else:
            print('MISSING CLASS in new:', x)
            api_diffs += 1
            missing_real.append(x)
    for x in extra:
        print('EXTRA CLASS in new:', x)
        api_diffs += 1
    for name in sorted(set(R) & set(N)):
        r, n = R[name], N[name]
        if not is_anonymous(name):
            a, b = api_view(r, False), api_view(n, False)
            if a != b:
                api_diffs += 1
                api_names.append(name)
                print('=== API DIFF', name)
                for l in difflib.unified_diff(a, b, 'jar', 'new', lineterm='', n=1):
                    print(l)
        else:
            a, b = api_view(r, False), api_view(n, False)
            if a != b:
                print('--- anonymous/synthetic class header diff (not API)', name)
                for l in difflib.unified_diff(a, b, 'jar', 'new', lineterm='', n=0):
                    print('   ', l)
        a, b = api_view(r, True), api_view(n, True)
        if a != b:
            syn_diffs += 1
            print('--- synthetic member diff', name)
            for l in difflib.unified_diff(a, b, 'jar', 'new', lineterm='', n=0):
                print('   ', l)
        if code:
            rm = normalized_members(r, rctx)
            nm = normalized_members(n, nctx)
            holder = is_anonymous(name) and all('$SwitchMap$' in m['decl'] or m['decl'].startswith('static {}') for m in r['members'])
            for k in sorted(set(rm) | set(nm)):
                if holder and k.startswith('<clinit>'):
                    # switch-map holder: its contents are compared through the 'enumswitch' rewrite
                    continue
                if k not in rm or k not in nm:
                    if not k.startswith('$SwitchMap$'):
                        code_only.append((name, k, 'jar' if k in rm else 'new'))
                    continue
                ra, nb = rm[k], nm[k]
                # local-variable names/types from the LocalVariableTable, compared as sets: javac 6 also
                # recorded the synthetic for-each iterator 'i$', bridge parameters 'x0'/'x1' and no
                # 'this$0'; javac >= 7 splits one variable into several ranges
                def lvt(c):
                    mm = dict((member_key(m), m) for m in c['members']).get(k, {'lvt': []})
                    return set(v for v in mm['lvt'] if v[0] not in ('i$', 'this$0') and not re.match(r'^x\d+$', v[0]))
                rl, nl = lvt(r), lvt(n)
                if rl and nl and rl != nl:
                    lvt_diffs.append((name, k, sorted(rl - nl), sorted(nl - rl)))
                if ra != nb:
                    code_diff_methods.append((name, k, ra, nb))
                else:
                    code_same += 1
    if code:
        print('\n##### CODE: %d members identical after normalization, %d differ' % (code_same, len(code_diff_methods)))
        for name, k, ra, nb in code_diff_methods[:show]:
            print('\n=== CODE DIFF %s :: %s' % (name, k))
            for l in difflib.unified_diff(ra, nb, 'jar', 'new', lineterm='', n=3):
                print(l)
        if code_diff_methods:
            print('\nDiffering methods:')
            for name, k, _, _ in code_diff_methods:
                print('   ', name, k)
        for name, k, side in code_only:
            print('only in %s: %s :: %s' % (side, name, k))
        for name, k, a, b in lvt_diffs:
            print('local-variable names differ: %s :: %s  jar-only=%s new-only=%s' % (name, k, a, b))
    print('\n##### SUMMARY: %d classes compared, %d missing, %d extra, API diffs in %d classes, synthetic diffs in %d classes'
          % (len(set(R) & set(N)), len(missing), len(extra), api_diffs, syn_diffs))
    if code:
        print('##### CODE SUMMARY: %d identical, %d differing methods, %d only on one side, %d with different local-variable names' % (code_same, len(code_diff_methods), len(code_only), len(lvt_diffs)))
    found = [('api', x) for x in api_names] + [('missing', x) for x in missing_real] + [('extra', x) for x in extra]
    if code:
        found += [('code', c, k) for c, k, _, _ in code_diff_methods] + [('only', c, k) for c, k, _ in code_only] \
            + [('lvt', c, k) for c, k, _, _ in lvt_diffs]
    unexplained = [f for f in found if f not in EXPECTED]
    print('\n##### EXPECTED (explained) differences: %d' % (len(found) - len(unexplained)))
    for f in found:
        if f in EXPECTED:
            print('   ', ' :: '.join(f[1:]), '[%s]' % f[0], '--', EXPECTED[f])
    print('##### UNEXPLAINED differences: %d' % len(unexplained))
    for f in unexplained:
        print('   ', f)
    sys.exit(1 if unexplained else 0)


if __name__ == '__main__':
    main()
