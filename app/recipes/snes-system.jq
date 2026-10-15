include "option";
def system($language; $region): $region | option("snes9x_region"; {japan: "ntsc", usa: "ntsc", europe: "pal"});
