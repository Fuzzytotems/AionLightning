package cppgen;

import com.sun.source.tree.*;
import com.sun.source.util.*;
import javax.lang.model.element.*;
import javax.lang.model.type.*;
import javax.tools.*;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;
import java.util.stream.*;

/**
 * Extracts every external (non-codebase) type and member referenced by the Java sources,
 * with use counts and one example location. Output: TSV "kind\towner\tmember\tcount\texample".
 * Usage: java -cp tools/cppgen/build cppgen.ApiUsage <classpath> <out.tsv> <srcdir>...
 */
public class ApiUsage {
    static final Set<String> CODEBASE_PREFIXES = Set.of("org.openaion.", "com.aionengine.chatserver.", "quest.", "admincommands", "usercommands", "mysql5", "languages", "ai.", "chathandlers");

    static boolean isCodebase(String fqn) {
        if (fqn.isEmpty()) return true;
        for (String p : CODEBASE_PREFIXES) if (fqn.startsWith(p)) return true;
        return false;
    }

    public static void main(String[] args) throws Exception {
        String cp = args[0];
        String out = args[1];
        List<File> files = new ArrayList<>();
        for (int i = 2; i < args.length; i++)
            try (Stream<Path> s = Files.walk(Paths.get(args[i]))) {
                s.filter(p -> p.toString().endsWith(".java")).forEach(p -> files.add(p.toFile()));
            }
        JavaCompiler jc = ToolProvider.getSystemJavaCompiler();
        StandardJavaFileManager fm = jc.getStandardFileManager(null, null, StandardCharsets.ISO_8859_1);
        JavacTask task = (JavacTask) jc.getTask(null, fm, d -> {}, List.of("--release", "8", "-proc:none", "-cp", cp, "-encoding", "ISO-8859-1"), null, fm.getJavaFileObjectsFromFiles(files));
        Iterable<? extends CompilationUnitTree> units = task.parse();
        task.analyze();
        Trees trees = Trees.instance(task);
        Map<String, int[]> counts = new TreeMap<>();
        Map<String, String> example = new HashMap<>();
        for (CompilationUnitTree cu : units) {
            new TreePathScanner<Void, Void>() {
                void rec(String kind, Element e, Tree t) {
                    if (e == null) return;
                    Element owner = e;
                    String member = "";
                    if (e.getKind() == ElementKind.METHOD || e.getKind() == ElementKind.CONSTRUCTOR || e.getKind() == ElementKind.FIELD || e.getKind() == ElementKind.ENUM_CONSTANT) {
                        owner = e.getEnclosingElement();
                        member = e.toString();
                    }
                    if (!(owner instanceof TypeElement)) return;
                    String fqn = ((TypeElement) owner).getQualifiedName().toString();
                    if (isCodebase(fqn)) return;
                    String key = kind + "\t" + fqn + "\t" + member;
                    counts.computeIfAbsent(key, k -> new int[1])[0]++;
                    if (!example.containsKey(key)) {
                        long line = cu.getLineMap().getLineNumber(trees.getSourcePositions().getStartPosition(cu, t));
                        example.put(key, cu.getSourceFile().getName().replaceFirst(".*AionLightning/", "") + ":" + line);
                    }
                }
                @Override public Void visitMethodInvocation(MethodInvocationTree t, Void v) { rec("call", trees.getElement(new TreePath(getCurrentPath(), t.getMethodSelect())), t); return super.visitMethodInvocation(t, v); }
                @Override public Void visitNewClass(NewClassTree t, Void v) { rec("new", trees.getElement(getCurrentPath()), t); return super.visitNewClass(t, v); }
                @Override public Void visitMemberSelect(MemberSelectTree t, Void v) {
                    Element e = trees.getElement(getCurrentPath());
                    if (e != null && (e.getKind() == ElementKind.FIELD || e.getKind() == ElementKind.ENUM_CONSTANT)) rec("field", e, t);
                    return super.visitMemberSelect(t, v);
                }
                @Override public Void visitIdentifier(IdentifierTree t, Void v) {
                    Element e = trees.getElement(getCurrentPath());
                    if (e instanceof TypeElement) rec("type", e, t);
                    else if (e != null && (e.getKind() == ElementKind.FIELD || e.getKind() == ElementKind.ENUM_CONSTANT)) rec("field", e, t);
                    return super.visitIdentifier(t, v);
                }
                @Override public Void visitClass(ClassTree t, Void v) {
                    Element e = trees.getElement(getCurrentPath());
                    if (e instanceof TypeElement te) {
                        TypeMirror sup = te.getSuperclass();
                        if (sup instanceof DeclaredType dt) rec("extends", dt.asElement(), t);
                        for (TypeMirror i : te.getInterfaces()) if (i instanceof DeclaredType dt) rec("implements", dt.asElement(), t);
                    }
                    return super.visitClass(t, v);
                }
            }.scan(cu, null);
        }
        try (PrintWriter pw = new PrintWriter(out)) {
            for (var en : counts.entrySet()) pw.println(en.getKey() + "\t" + en.getValue()[0] + "\t" + example.get(en.getKey()));
        }
        System.err.println("entries: " + counts.size());
    }
}
