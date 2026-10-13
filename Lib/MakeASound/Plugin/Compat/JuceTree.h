#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace MakeASound::Compat
{

// What JUCE keeps a plugin's state in, read back with nothing of JUCE: a
// ValueTree, or the XmlElement it prints as. A node is a type (the element's
// tag), properties in document order and children; an XML text node is skipped,
// since ValueTree::fromXml drops it too. A property is the text JUCE's var would
// print: an int in decimal, a double in its shortest round-trip form, a bool as
// "1" or "0", a string as itself and binary data as its raw bytes.
struct JuceTree
{
    std::string type;
    std::vector<std::pair<std::string, std::string>> properties;
    std::vector<JuceTree> children;

    // The last property of that name, as JUCE's NamedValueSet keeps one; null
    // when absent.
    const std::string* property(std::string_view name) const noexcept;

    // The first child of that type; null when none.
    const JuceTree* child(std::string_view childType) const noexcept;
};

// XmlElement's text as JUCE writes it, single line or indented: the `<?xml ?>`
// declaration, a DOCTYPE, comments, CDATA and processing instructions skipped;
// attributes in double or single quotes; the five named entities and decimal or
// hex character references (JUCE writes every non-ASCII character as one)
// decoded to UTF-8. The first element is the document, and whatever follows it
// is ignored, as XmlDocument ignores it; nullopt when there is no well-formed
// first element.
std::optional<JuceTree> parseJuceXml(std::string_view text);

// ValueTree::writeToStream's bytes: the type as a NUL-terminated string, a
// compressed property count, each property's name and var, a compressed child
// count and the children. Bytes past the tree are ignored, as JUCE ignores them;
// nullopt for an empty type, a negative count or bytes that end mid-tree.
std::optional<JuceTree> readJuceValueTree(std::string_view bytes);

// A property's text to a number as JUCE's var does it: leading whitespace
// skipped, the longest numeric prefix read, 0 for none. Not locale-dependent.
double juceStringToDouble(std::string_view text) noexcept;

} // namespace MakeASound::Compat
