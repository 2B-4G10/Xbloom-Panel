// UI translations. Each English string used on screen maps to its Arabic text.
// Format strings keep the same printf specifiers, in the same order.
#include "i18n.h"

#include <string.h>

#include <algorithm>

namespace i18n {
namespace {

struct Entry {
  const char *en, *ar;
};

// clang-format off
Entry table[] = {
  // Home
  {"No favorites yet", "لا توجد وصفات مفضلة بعد"},
  {"Connecting", "جارٍ الاتصال"},
  {"Connected", "متصل"},
  {"Searching", "جارٍ البحث"},
  {"Starting", "جارٍ البدء"},
  {"Grinder", "المطحنة"},
  {"Brewer", "المحضّر"},
  {"Scale", "الميزان"},
  {"Favorite Recipes", "الوصفات المفضلة"},
  {"xBloom", "xBloom"},
  {"Recipes", "الوصفات"},
  {"Settings", "الإعدادات"},
  {"Double Shot", "دبل شوت"},

  // Recipes
  {"My Recipes", "وصفاتي"},
  {"+  New", "+  جديدة"},
  {"Created", "من إنشائي"},
  {"Shared", "مُشاركة"},
  {"Starter", "الافتراضية"},
  {"Not saved to account", "غير محفوظة في الحساب"},
  {"Recipes saved from share links appear here.", "الوصفات المحفوظة من روابط المشاركة تظهر هنا."},
  {"Sign in under Settings to bring in your recipes.", "سجّل الدخول من الإعدادات لجلب وصفاتك."},
  {"1:%g-%.0fml   |   %gg   |   %d Pours", "1:%g-%.0fml   |   %gg   |   %d صبّات"},

  // Settings
  {"CONNECTION", "الاتصال"},
  {"Wi-Fi", "الواي فاي"},
  {"Software update", "تحديث البرنامج"},
  {"xBloom account", "حساب xBloom"},
  {"Sync now", "مزامنة الآن"},
  {"SCREEN", "الشاشة"},
  {"Clock when idle", "الساعة عند الخمول"},
  {"Sleep after", "إطفاء الشاشة بعد"},
  {"1 min\n3 min\n5 min\n10 min\nNever", "دقيقة\n3 دقائق\n5 دقائق\n10 دقائق\nأبدًا"},
  {"Clock colour", "لون الساعة"},
  {"White\nBlue\nBrown", "أبيض\nأزرق\nبني"},
  {"Off at night (11 PM-6 AM)", "إطفاء ليلًا (11 م - 6 ص)"},
  {"LANGUAGE", "اللغة"},
  {"Language", "اللغة"},
  {"Restarting...", "جارٍ إعادة التشغيل..."},
  {"ABOUT", "حول"},
  {"Machine", "المكينة"},
  {"Not connected", "غير متصل"},
  {"Not signed in", "لم يتم تسجيل الدخول"},
  {"Syncing...", "جارٍ المزامنة..."},
  {"Synced %s", "تمت المزامنة %s"},
  {"not yet", "لم تتم بعد"},
  {"at %d:%02d %s", "الساعة %d:%02d %s"},
  {"AM", "ص"},
  {"PM", "م"},
  {"Off", "إيقاف"},

  // Wi-Fi
  {"Looking for networks...", "جارٍ البحث عن الشبكات..."},
  {"No networks found", "لم يتم العثور على شبكات"},
  {"Secured", "محمية"},
  {"Open", "مفتوحة"},
  {"Choose your network from the list.", "اختر شبكتك من القائمة."},
  {"Connected\n%s", "متصل\n%s"},
  {"Connecting...", "جارٍ الاتصال..."},
  {"Wrong password. Tap the network to try again.", "كلمة المرور غير صحيحة. اضغط على الشبكة للمحاولة مرة أخرى."},
  {"Network not found. Is it in range?", "لم يتم العثور على الشبكة. هل هي ضمن النطاق؟"},
  {"Forget network", "نسيان الشبكة"},
  {"Scan again", "البحث مرة أخرى"},
  {"Scanning...", "جارٍ البحث..."},
  {"Show", "إظهار"},
  {"Hide", "إخفاء"},
  {"Join", "انضمام"},
  {"Join \"%s\"", "الانضمام إلى \"%s\""},
  {"Password", "كلمة المرور"},
  {"Wi-Fi settings", "إعدادات الواي فاي"},
  {"Wrong Wi-Fi password", "كلمة مرور الواي فاي غير صحيحة"},

  // Software update
  {"Connect the panel to Wi-Fi first.", "اربط الشاشة بالواي فاي أولًا."},
  {"On your phone or computer (same Wi-Fi), open:", "من جوالك أو كمبيوترك (على نفس الواي فاي)، افتح:"},
  {"Waiting for the file...", "بانتظار الملف..."},
  {"or  http://%s     then choose the update file.", "أو  http://%s     ثم اختر ملف التحديث."},
  {"Installing... %d%%   Keep the panel powered.", "جارٍ التثبيت... %d%%   أبقِ الشاشة موصولة بالكهرباء."},
  {"Installed. Restarting...", "تم التثبيت. جارٍ إعادة التشغيل..."},
  {"Failed: %s", "فشل: %s"},
  {"Installed version %s", "الإصدار المثبّت %s"},
  {"If an update ever fails to start, the panel goes back to this version by itself.",
   "إذا لم يعمل التحديث، تعود الشاشة إلى هذا الإصدار تلقائيًا."},

  // Double Shot page
  {"Scan a code with your phone to shop online or chat on WhatsApp.", "امسح الرمز بجوالك للتسوق أونلاين أو للتواصل عبر واتساب."},
  {"SHOP BY CATEGORY", "تسوّق حسب الفئة"},
  {"Roasters", "المحامص"},
  {"Grinders", "المطاحن"},
  {"Drip & brew", "أدوات التقطير"},
  {"Espresso tools", "أدوات الإسبريسو"},
  {"Kettles & cups", "الغلايات والأكواب"},
  {"Cleaning", "التنظيف"},
  {"FEATURED BRANDS", "علامات مميزة"},
  {"Shop online", "تسوّق أونلاين"},
  {"WhatsApp", "واتساب"},

  // Brewing a recipe
  {"Place your cup", "ضع كوبك"},
  {"The recipe is loaded on the machine.\nPut your cup on the scale, then start.",
   "تم تحميل الوصفة على المكينة.\nضع كوبك على الميزان، ثم ابدأ."},
  {"Cancel", "إلغاء"},
  {"Brew", "تحضير"},
  {"POURS", "الصبّات"},
  {"START", "ابدأ"},
  {"Pours", "الصبّات"},
  {"Bloom", "البلوم"},
  {"Dose", "الجرعة"},
  {"Grind Size", "درجة الطحن"},
  {"Water", "الماء"},
  {"Time", "الوقت"},
  {"LOADING", "تحميل"},
  {"READY", "جاهز"},
  {"STARTING", "بدء"},
  {"PAUSE", "إيقاف مؤقت"},
  {"RESUME", "استئناف"},
  {"BUSY", "مشغول"},
  {"Not connected to the xBloom", "غير متصل بمكينة xBloom"},
  {"Another recipe is on the machine", "توجد وصفة أخرى على المكينة"},
  {"%d pours  |  %.0f ml", "%d صبّات  |  %.0f ml"},
  {"%.0f\xC2\xB0" "C  |  %s  |  %d of %d", "%.0f\xC2\xB0" "C  |  %s  |  %d من %d"},
  {"Pour %d", "الصبّة %d"},
  {"Your coffee", "قهوتك"},
  {"Command failed", "فشل الأمر"},
  {"Enjoy", "بالعافية"},
  {"Paused", "متوقف مؤقتًا"},
  {"min", "د"},

  // Cups, grinder, brewer
  {"Espresso", "إسبريسو"},
  {"Moka Pot", "موكا بوت"},
  {"Pourover\nCoffee Maker", "صانعة\nالقهوة بالتقطير"},
  {"French Press", "فرنش برس"},
  {"Cold Brew", "كولد برو"},
  {"Other", "أخرى"},
  {"Tea", "شاي"},
  {"Grind size", "درجة الطحن"},
  {"Level", "المستوى"},
  {"Grinder Speed", "سرعة المطحنة"},
  {"Volume", "الكمية"},
  {"Temperature", "الحرارة"},
  {"Flow rate", "معدل التدفق"},
  {"Centered", "مركزي"},
  {"Circular", "دائري"},
  {"Spiral", "حلزوني"},

  // Recipe editor
  {"Delete", "حذف"},
  {"Untitled", "بدون اسم"},
  {"Saved on the panel. Sign in to save it to your account too.", "حُفظت على الشاشة. سجّل الدخول لحفظها في حسابك أيضًا."},
  {"Edit recipe", "تعديل الوصفة"},
  {"Save", "حفظ"},
  {"Delete recipe?", "حذف الوصفة؟"},
  {"It will also be deleted from your xBloom account.", "ستُحذف من حساب xBloom أيضًا."},
  {"It will be removed from the panel.", "ستُحذف من الشاشة."},
  {"Recipe name", "اسم الوصفة"},
  {"Name", "الاسم"},
  {"Done", "تم"},
  {"Wait after", "الانتظار بعدها"},
  {"Agitate before", "التحريك قبل"},
  {"Agitate after", "التحريك بعد"},
  {"Remove this pour", "حذف هذه الصبّة"},
  {"New recipe", "وصفة جديدة"},

  // xBloom account
  {"%d created  |  %d shared\nLast synced %s%s", "%d من إنشائي  |  %d مُشاركة\nآخر مزامنة %s%s"},
  {"  (syncing...)", "  (جارٍ المزامنة...)"},
  {"Signing in...", "جارٍ تسجيل الدخول..."},
  {"Sign in with your xBloom app email and password to bring your recipes to the panel.  ",
   "سجّل الدخول ببريد وكلمة مرور تطبيق xBloom لجلب وصفاتك إلى الشاشة.  "},
  {"Edits sync both ways.", "التعديلات تتزامن في الاتجاهين."},
  {"Sign in", "تسجيل الدخول"},
  {"Connect to Wi-Fi first (Settings > Wi-Fi)", "اتصل بالواي فاي أولًا (الإعدادات > الواي فاي)"},
  {"Email", "البريد الإلكتروني"},
  {"Next", "التالي"},
  {"xBloom password", "كلمة مرور xBloom"},
  {"Sign out", "تسجيل الخروج"},
  {"Sign out?", "تسجيل الخروج؟"},
  {"Your account's recipes will be removed from the panel. Recipes made on the panel stay.",
   "ستُحذف وصفات حسابك من الشاشة. الوصفات التي أنشأتها على الشاشة ستبقى."},
  {"The panel saves your password so it can sign in again by itself.", "تحفظ الشاشة كلمة المرور لتسجّل الدخول تلقائيًا."},
  {"Saved on the panel, but not to your account: %s", "حُفظت على الشاشة، لكن ليس في حسابك: %s"},
  {"Couldn't delete it from your account: %s", "تعذّر حذفها من حسابك: %s"},

  // Messages from the machine
  {"No water", "لا يوجد ماء"},
  {"Go to the home screen and retry", "ارجع إلى الشاشة الرئيسية وحاول مرة أخرى"},
  {"Machine not ready", "المكينة غير جاهزة"},
  {"Power-cycle restore incomplete", "لم تكتمل الاستعادة بعد إعادة التشغيل"},
  {"Recipe rejected", "رُفضت الوصفة"},
  {"Machine busy", "المكينة مشغولة"},
  {"Out of water", "نفد الماء"},
  {"No beans", "لا توجد حبوب"},
  {"Dose/water error", "خطأ في الجرعة أو الماء"},
  {"Grinder gear error", "خطأ في ترس المطحنة"},
  {"Grinding", "جارٍ الطحن"},
  {"Brewing", "جارٍ التحضير"},
  {"1-9 pours required", "يلزم من 1 إلى 9 صبّات"},
  {"dose out of range for cup", "الجرعة خارج النطاق لهذا الكوب"},
  {"grind size must be 1-80", "درجة الطحن يجب أن تكون بين 1 و 80"},
  {"grinder rpm must be 60-120", "سرعة المطحنة يجب أن تكون بين 60 و 120"},
  {"pour volume must be 0-240 ml", "كمية الصبّة يجب أن تكون بين 0 و 240 ml"},
  {"temperature must be 20-98 C", "الحرارة يجب أن تكون بين 20 و 98 C"},
  {"flow must be 3.0-3.5", "معدل التدفق يجب أن يكون بين 3.0 و 3.5"},
  {"pause must be 0-59 s", "الانتظار يجب أن يكون بين 0 و 59 s"},
  {"no water", "لا يوجد ماء"},
  {"ratio must be 1:5 - 1:25", "النسبة يجب أن تكون بين 1:5 و 1:25"},
};
// clang-format on

constexpr size_t N = sizeof table / sizeof table[0];
Lang cur = Lang::EN;
bool sorted = false;

bool less(const Entry &a, const Entry &b) { return strcmp(a.en, b.en) < 0; }

}  // namespace

void set_lang(Lang l) {
  cur = l;
  if (!sorted) {
    std::sort(table, table + N, less);
    sorted = true;
  }
}

Lang lang() { return cur; }

const char *tr(const char *en) {
  if (cur == Lang::EN || !en || !*en || !sorted) return en;
  const Entry key{en, nullptr};
  const Entry *e = std::lower_bound(table, table + N, key, less);
  return (e != table + N && strcmp(e->en, en) == 0) ? e->ar : en;
}

}  // namespace i18n
