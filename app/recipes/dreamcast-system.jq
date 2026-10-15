include "option";
def system($language; $region):
  ($language | pick({ja: "japanese", en: "english", de: "german", fr: "french", es: "spanish", it: "italian"}) // empty | "language=\(.)"),
  ($region | pick({japan: "japan", usa: "usa", europe: "europe"}) // empty | "region=\(.)");
