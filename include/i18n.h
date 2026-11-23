// Interface language: English or Arabic (right-to-left).
#pragma once
#include <stddef.h>

namespace i18n {
enum class Lang { EN = 0, AR = 1 };

void set_lang(Lang l);  // call once at start-up, before the screens are built
Lang lang();
inline bool rtl() { return lang() == Lang::AR; }
// Arabic only: numbers and units in Arabic too ("٨٠ مل"), or kept as "80 ml".
void set_arabic_units(bool on);
bool arabic_units();

// The text to show for an English UI string. Returns `en` itself when there is no
// translation (or in English), so it is safe to pass any text, including user data.
const char *tr(const char *en);

// With Arabic units on: numbers and units in Arabic, e.g. "80ml" -> "٨٠ مل", "88.4" -> "٨٨٫٤",
// "95°C" -> "٩٥°م". Text with other Latin letters (names, Wi-Fi networks, emails) or
// addresses like "192.168.1.50" comes back unchanged. Returns `in` or `buf`.
const char *localize(const char *in, char *buf, size_t n);
}  // namespace i18n
