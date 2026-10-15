include "option";
def system($language; $region): $language | pick({ja: 0, en: 1, fr: 2, de: 3, it: 4, es: 5, zh_CN: 6, ko: 7, nl: 8, pt: 9, ru: 10, zh_TW: 11}) // empty | "\t<console_language>\(.)</console_language>";
