#pragma once

#include "../../Common/Common.h"

#include <string>
#include <string_view>

namespace MakeASound
{

// A parameter's id path joins the ids of its enclosing named groups and its own
// with '/' ("Filter/Cutoff"). An id may hold a '/' itself (Tamber Plug's
// "Dry/Wet", which is also the parameter's key in a saved document), so inside
// one segment a '/' is written twice: "Dry//Wet" at the root, "Mix/Dry//Wet"
// inside a group "Mix". A group "Dry" holding "Wet" stays "Dry/Wet". An id with
// no '/' is its own segment, so no path, host id or AUv3 identifier of such ids
// moved. Saved state is nested objects keyed by each id as written, so a
// document's keys never see the escaping.
//
// One case stays ambiguous: an id that begins or ends with '/' ("a/" then "b"
// reads as "a" then "/b"). ParameterList::identityProblems() reports it; rename
// the id, or pin `id` to one without the edge '/' and keep the old key for a
// document with a state migration.

// The id as a segment of a path: every '/' doubled.
std::string escapeIdSegment(std::string_view id);

// Appends the id's segment to the path, with a separator unless either is empty.
void appendIdSegment(std::string& path, std::string_view id);

// The ids a path joins, outermost first, each as written: what
// escapeIdSegment() and appendIdSegment() built, read back.
Vector<std::string> splitIdPath(std::string_view path);

// False for an id whose segment a path cannot delimit: one that begins or ends
// with '/'.
bool isDelimitableId(std::string_view id) noexcept;

} // namespace MakeASound
