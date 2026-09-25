// Produces jlang/tests/data/xml/merge_expected.xml with the real gameserver XmlMerger (JDK 21,
// built-in StAX), for jlang/tests/test_xml_stax.cpp:
//
//   javac -cp <gameserver classes>:commons-io-2.0.1.jar:log4j-1.2.16.jar -d out RunMerger.java
//   cd jlang/tests/data/xml/merge
//   java -cp out:<same> RunMerger static_data.xml ../merge_expected.xml
//
// (the merger also writes ../merge_expected.xml.properties, which the test does not use).
import java.io.File;
import org.openaion.gameserver.dataholders.loadingutils.XmlMerger;

public class RunMerger {
    public static void main(String[] a) throws Exception {
        XmlMerger m = a.length > 2 ? new XmlMerger(new File(a[0]), new File(a[1]), new File(a[2]))
                                   : new XmlMerger(new File(a[0]), new File(a[1]));
        m.process();
    }
}
