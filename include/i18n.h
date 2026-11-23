// Interface language: English or Arabic (right-to-left).
#pragma once

namespace i18n {
enum class Lang { EN = 0, AR = 1 };

void set_lang(Lang l);  // call once at start-up, before the screens are built
Lang lang();
inline bool rtl() { return lang() == Lang::AR; }

// The text to show for an English UI string. Returns `en` itself when there is no
// translation (or in English), so it is safe to pass any text, including user data.
const char *tr(const char *en);
}  // namespace i18n
