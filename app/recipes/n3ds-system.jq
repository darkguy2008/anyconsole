include "option";
def system($language; $region):
  ($language | option("citra_language_value"; {en: "English", ja: "Japanese", fr: "French", es: "Spanish", de: "German", it: "Italian", nl: "Dutch", pt: "Portuguese", ru: "Russian", ko: "Korean", zh_TW: "Traditional Chinese", zh_CN: "Simplified Chinese"}))
  + ($region | option("citra_region_value"; {japan: "Japan", usa: "USA", europe: "Europe", australia: "Australia", china: "China", korea: "Korea", taiwan: "Taiwan"}));
