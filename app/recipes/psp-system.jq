include "option";
def system($language; $region): $language | pick({ja: 0, en: 1, fr: 2, es: 3, de: 4, it: 5, nl: 6, pt: 7, ru: 8, ko: 9, zh_TW: 10, zh_CN: 11}) // empty | "GameLanguage = \(.)";
