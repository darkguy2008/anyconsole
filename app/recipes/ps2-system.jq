include "option";
def system($language; $region): ($language | pick({en: 1, fr: 2, es: 3, de: 4, it: 5, nl: 6, pt: 7})) as $western | {region: $region, languages: {usa: $western, europe: $western, japan: ($language | pick({ja: 0, en: 1}))}};
