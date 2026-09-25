// jlang/Xml.h - javax.xml / org.xml.sax / JAXB support (namespace jlang::xml).
//
//   <jlang/Jaxb.h>       JAXB runtime for generated binders (Element, JaxbContext, Writer,
//                        lexical rules), JAXBContext/Unmarshaller/Marshaller facades,
//                        XmlAdapter, Schema/SchemaFactory (no-op validation), XMLConstants.
//   <jlang/XmlEvents.h>  StAX (XMLEventReader/Writer, events, QName) and SAX (SAXParser,
//                        DefaultHandler, Attributes, Locator) emulation.
//
// Implementation: jlang/src/xml_*.cpp over pugixml. Design and generator contract:
// docs/cpp-port/JAXB.md.
#pragma once

#include <jlang/Jaxb.h>
#include <jlang/XmlEvents.h>
