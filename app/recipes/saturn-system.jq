include "option";
def system($language; $region):
  ($language | option("beetle_saturn_autortc_lang"; {en: "english", de: "german", fr: "french", es: "spanish", it: "italian", ja: "japanese"}))
  + ($region | option("beetle_saturn_region"; {japan: "Japan", usa: "North America", europe: "Europe"}));
