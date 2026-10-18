#pragma once
/*
 * Trail library for the trail info bot (item 32): South East Queensland (Gold Coast, Scenic Rim, Brisbane,
 * Sunshine Coast and the bay islands).
 *
 * Compiled into the firmware (flash). To change a trail, edit this file and rebuild.
 *
 * Rules (tools/check_trails.py checks them):
 *   - Names, aliases and info are plain ASCII (an apostrophe in a name is fine).
 *   - 'aliases' are other names people type, lowercase, separated by '|'. No apostrophes, no punctuation.
 *   - 'info' is "<distance>, <time>, <grade>, <features>". "<name>: <info>" must be 110 characters or fewer.
 *   - No alias may be used by two different trails, two different towns, or two different regions.
 *   - Three levels: regions (!trails), towns (!<region>), trails (!<town>). Lookup order is trail, then town, then region,
 *     so a trail name or alias that is also a town name wins: "!natural bridge" gives the trail.
 *   - A region lists its towns, a town lists its trails. Keep a town to about 12 trails or fewer (a list is at most 4 messages).
 *   - Grades are the Queensland Parks 1 to 5 scale. "ungraded" means there is no official grade.
 *   - Facts come from Queensland Parks and Wildlife Service pages, the City of Gold Coast, and hiker logs
 *     where no official figure exists (those rows say "~" or "off-track"). CHECK PARK ALERTS BEFORE WALKING.
 */

#include <stdint.h>

struct TrailRegion {
  const char* name;      // shown in lists
  const char* aliases;   // '|' separated, lowercase
};

struct TrailTown {
  const char* name;      // shown in lists
  const char* aliases;   // '|' separated, lowercase
  uint8_t region;        // index into TRAIL_REGIONS
};

struct Trail {
  uint8_t town;          // index into TRAIL_TOWNS
  const char* name;      // shown in lists and as the start of the reply
  const char* aliases;   // '|' separated, lowercase
  const char* info;      // the rest of the reply
};

static const TrailRegion TRAIL_REGIONS[] = {
  /* 0 */ {"Gold Coast", "gc"},
  /* 1 */ {"Scenic Rim", ""},
  /* 2 */ {"Brisbane", "brissy|brisbane city|bne"},
  /* 3 */ {"Sunshine Coast", "sunny coast|sunshine coast hinterland"},
  /* 4 */ {"Moreton Bay Islands", "moreton bay|bay islands|islands"},
  /* 5 */ {"Ipswich and Somerset", "ipswich|somerset"},
};

static const TrailTown TRAIL_TOWNS[] = {
  // ---- Gold Coast ----
  /* 0  */ {"Springbrook", "springbrook np|springbrook national park", 0},
  /* 1  */ {"Natural Bridge", "", 0},
  /* 2  */ {"Mount Cougal", "mt cougal|cougal|currumbin valley", 0},
  /* 3  */ {"Burleigh Heads", "burleigh|burleigh head", 0},
  /* 4  */ {"Tallebudgera Valley", "tallebudgera", 0},
  /* 5  */ {"Wongawallan", "", 0},
  /* 6  */ {"Bonogin", "", 0},
  // ---- Scenic Rim ----
  /* 7  */ {"Tamborine Mountain", "tamborine|mt tamborine|mount tamborine|tamborine np|tamborine national park", 1},
  /* 8  */ {"O'Reilly's", "green mountains", 1},
  /* 9  */ {"Binna Burra", "binna", 1},
  /* 10 */ {"Mount Barney", "mt barney|barney", 1},
  /* 11 */ {"Cunninghams Gap", "cunningham gap|main range|main range np|main range national park|spicers gap", 1},
  /* 12 */ {"Moogerah", "moogerah peaks|moogerah peaks np|lake moogerah|mount french|mt french", 1},
  // ---- Brisbane ----
  /* 13 */ {"Mount Coot-tha", "mt coot tha|coot tha|cootha|mt cootha|mount cootha", 2},
  /* 14 */ {"Mount Glorious", "mt glorious|maiala|daguilar|daguilar np|daguilar national park", 2},
  /* 15 */ {"Mount Nebo", "mt nebo|jollys lookout|jollys", 2},
  /* 16 */ {"The Gap", "walkabout creek", 2},
  // ---- Sunshine Coast ----
  /* 17 */ {"Glass House Mountains", "glasshouse mountains|glass house|glasshouse|ghm|glass house mountains np", 3},
  /* 18 */ {"Noosa", "noosa heads|noosa national park|noosa np", 3},
  /* 19 */ {"Kondalilla", "montville|flaxton|kondalilla np|kondalilla national park", 3},
  /* 20 */ {"Mapleton", "", 3},
  /* 21 */ {"Conondale", "booloumba|conondale np", 3},
  /* 22 */ {"Coolum", "coolum beach|mount coolum np", 3},
  /* 23 */ {"Pomona", "noosa hinterland", 3},
  // ---- Moreton Bay Islands ----
  /* 24 */ {"North Stradbroke Island", "stradbroke|north stradbroke|straddie|north straddie|minjerribah|point lookout", 4},
  /* 25 */ {"Moreton Island", "moreton|mulgumpin|tangalooma|gheebulum kunungai", 4},
  // ---- added later (kept at the end so the town numbers above never change) ----
  /* 26 */ {"Toohey Forest", "toohey|mount gravatt|mt gravatt|tarragindi", 2},
  /* 27 */ {"Bunyaville", "bunyaville conservation park|keperra", 2},
  /* 28 */ {"Maleny", "", 3},
  /* 29 */ {"Buderim", "buderim forest park", 3},
  /* 30 */ {"Kenilworth", "imbil|mary valley", 3},
  /* 31 */ {"Bribie Island", "bribie|bribie island national park|bongaree|bicentennial trails", 4},
  /* 32 */ {"Flinders-Goolman", "flinders goolman conservation estate|peak crossing|ipswich peaks", 5},
  /* 33 */ {"Mount Mee", "mt mee|neurum creek|the gantry|mount mee forest reserve", 5},
  /* 34 */ {"Numinbah Valley", "numinbah|neranwood|upper nerang|advancetown", 0},
};

static const Trail TRAILS[] = {
  // ---- Springbrook ----
  {0, "Twin Falls Circuit", "twin falls", "4km return, 2h, Gr 3, waterfalls, rainforest"},
  {0, "Purling Brook Falls Circuit", "purling brook falls|purling brook", "4km return, 2h, Gr 3, waterfall, gorge"},
  {0, "Warringa Pool", "warringa pool track|warringa", "6km return, 3h, Gr 3, rock pool, rainforest"},
  {0, "Warrie Circuit", "warrie", "14km circuit, 5-6h, Gr 4, waterfalls, creeks"},
  {0, "Best of All Lookout", "best of all", "0.6km return, 30min, Gr 2, lookout, beech forest"},
  {0, "Goomoolahra Falls", "goomoolahra lookout", "0.25km return, 5min, Gr 1, waterfall, lookout"},
  {0, "Boojerahla Lookout", "boojerahla", "0.45km return, 20min, Gr 2, escarpment views"},
  {0, "Buliya-buliya Boardwalk", "buliya buliya|buliya", "0.1km return, 5min, Gr 2, boardwalk, birds"},
  {0, "Canyon Lookout", "the canyon", "0.03km return, 5min, Gr 1, cliffs, views"},
  {0, "Springbrook Pinnacle", "springbrook pinnacle track", "~20km return, ~7h, ungraded, off-track, scrambling, 360 views"},

  // ---- Natural Bridge ----
  {1, "Natural Bridge Circuit", "natural bridge|natural arch|glow worm cave|glowworm cave", "1km circuit, 1h, Gr 2, cave, waterfall"},

  // ---- Mount Cougal ----
  {2, "Cougal Cascades Track", "cougal cascades|cascades", "1.6km return, 1h, Gr 2, cascades, old sawmill"},
  {2, "Mount Cougal Summit", "cougal summit|cougal east|east cougal", "~10km return, 5h, ungraded, off-track, hard navigation, views"},
  {2, "Boyd's Butte", "boyds view", "~8km return, 4h, ungraded, off-track, exposed top, views"},
  {2, "Cougal Boyds Circuit", "boyds butte circuit|cascades boyds circuit", "14.5km circuit, 6h, ungraded, off-track, easy to lose trail"},

  // ---- Tamborine Mountain ----
  {7, "Curtis Falls Track", "curtis falls|curtis waterfall", "1.1km return, 30min, Gr 3, waterfall, rainforest"},
  {7, "Lower Creek Circuit", "lower creek", "2.5km return, 1h, Gr 4, creek, strangler fig"},
  {7, "Cedar Creek Falls Lookout", "cedar creek falls|cedar creek lookout", "0.5km return, 15min, Gr 1, waterfall, lookout"},
  {7, "Cedar Creek Rock Pools", "cedar creek falls rock pools", "1.1km return, 30min, Gr 3, rock pools, creek"},
  {7, "MacDonald Rainforest Circuit", "macdonald circuit", "1.4km return, 30min, Gr 3, strangler figs, palms"},
  {7, "Palm Grove Circuit", "palm grove", "2.7km circuit, 1h, Gr 4, palms, rainforest"},
  {7, "Jenyns Circuit", "jenyns", "4.8km circuit, 1.5h, Gr 4, rainforest, eucalypt forest"},
  {7, "Witches Falls Circuit", "witches falls", "3.6km return, 1h, Gr 4, waterfall, lagoons"},
  {7, "Witches Chase Track", "witches chase", "2.6km return, 1h, Gr 4, red cedars, lookout"},
  {7, "Sandy Creek Circuit", "sandy creek|cameron falls", "2.6km circuit, 1h, Gr 4, creek, Cameron Falls lookout"},

  // ---- O'Reilly's (Green Mountains, Lamington) ----
  {8, "Centenary Track", "centenary", "1.8km return, 30min, Gr 1, sealed, accessible"},
  {8, "Python Rock Track", "python rock", "3.1km return, 1h, Gr 3, lookout, rainforest"},
  {8, "Moran's Falls Track", "morans falls", "4.4km return, 1.5h, Gr 4, 80m waterfall, gorge"},
  {8, "Rainforest Return", "rainforest return track", "1.4km circuit, 30min, Gr 2, rainforest"},
  {8, "Box Forest Circuit", "box forest", "10.9km return, 4h, Gr 4, waterfalls, brush box"},

  // ---- Binna Burra (Lamington) ----
  {9, "Binna Burra Rainforest Circuit", "binna burra rainforest|binna rainforest circuit", "1.2km circuit, 30min, Gr 2, rainforest"},
  {9, "Tullawallal Circuit", "tullawallal", "5km circuit, 1.5h, Gr 3, rainforest"},
  {9, "Caves Track", "caves|binna caves", "7km return, 2h, Gr 4, caves, lookout"},
  {9, "Gwongoorool Track", "gwongoorool", "6km return, 2h, Gr 4, pool, rainforest"},
  {9, "Daves Creek Circuit", "daves creek", "12km circuit, 4h, Gr 4, lookout, rainforest"},
  {9, "Border Track", "border", "21.4km one way, 7h, Gr 4, ridgelines, rainforest"},
  {9, "Ships Stern Circuit", "ships stern", "21km circuit, 8h, Gr 4, lookouts, Nagarigoon Falls"},
  {9, "Nagarigoon Falls", "nagarigoon", "no own track: side trip on Ships Stern Circuit, Gr 4, waterfall"},

  // ---- Mount Barney ----
  {10, "Yellow Pinch Lookout", "yellow pinch", "2km return, ~1h, Gr 4, Barney views"},
  {10, "Cronan Creek Falls", "cronan creek", "12km return, 4h, Gr 4, creek, waterfall"},
  {10, "Lower Portals", "lower portals track", "7.4km return, ~3h, Gr 4, gorge, deep pool"},
  {10, "Upper Portals", "upper portals track", "8km return, time unknown, Gr 4, gorge, 4WD access"},
  {10, "Mount Maroon Summit Track", "mount maroon|mt maroon|maroon", "5.6km return, ~3h, Gr 4-5, steep scramble, 360 views"},

  // ---- Burleigh Heads ----
  {3, "Oceanview Track", "oceanview|oceanview walk|ocean view", "1.2km one way, 30min, Gr 2, coast, basalt columns"},
  {3, "Burleigh Rainforest Circuit", "burleigh rainforest|tumgun lookout|tumgun", "2.3km return, 45min, Gr 3, rainforest, Tumgun lookout"},

  // ---- Tallebudgera Valley ----
  {4, "Gorge Falls", "tallebudgera gorge falls", "4-5.6km return, 2h, ungraded, creek crossings, swimming"},

  // ---- Wongawallan ----
  {5, "Mount Wongawallan Track", "mount wongawallan|mt wongawallan", "1.5km return, 30min, ungraded, steep, lookout"},

  // ---- Bonogin ----
  {6, "Bally Mountain Summit Track", "bally mountain|bally mtn", "6km return, 3-4h, Gr 5, rock scramble, views"},

  // ---- Cunninghams Gap (Main Range National Park) ----
  {11, "Mount Cordeaux Track", "mount cordeaux|mt cordeaux|cordeaux", "6.8km return, 2.5h, Gr 4, rainforest, lookout"},
  {11, "Bare Rock Track", "bare rock|morgans walk", "12.4km return, 4.5h, Gr 4, rainforest, Bare Rock views"},
  {11, "Gap Creek Falls Track", "gap creek falls", "9.8km return, 6h, Gr 4, steep, falls and valley views"},
  {11, "Mount Mitchell Track", "mount mitchell|mt mitchell", "10.2km return, 3.5h, Gr 4, montane heath, views"},

  // ---- Moogerah Peaks ----
  {12, "Mount Edwards Summit Trail", "mount edwards|mt edwards", "6km return, 3.5h, Gr 5, steep, summit views"},
  {12, "Mount Greville Track", "mount greville|mt greville", "~5km return, ~4h, Gr 5, steep scramble, valley views"},
  {12, "North Cliff Track", "north cliff|mount french north cliff", "0.72km return, ~20min, Gr 1, Fassifern Valley views"},

  // ---- Mount Coot-tha (Brisbane) ----
  {13, "Mount Coot-tha Summit Track", "summit track|coot tha summit|cootha summit|mt coot tha summit", "1.9km one way, 30min, Gr 3, forest, city views"},
  {13, "JC Slaughter Falls Track", "jc slaughter falls|slaughter falls", "0.6km one way, ~15min, ungraded, falls only after rain"},
  {13, "Simpsons Falls Track", "simpson falls|simpsons falls", "0.65km to falls, 40min, ungraded, falls, city view deck"},
  {13, "Litchfield Track", "litchfield", "2.5km one way, ~45min, moderate, bushland, trail runners"},

  // ---- Mount Glorious (D'Aguilar National Park, Maiala) ----
  {14, "Greenes Falls Track", "greenes falls|greens falls", "4.3km return, 2h, Gr 3, rainforest, falls lookout, uphill return"},
  {14, "Westside Track", "westside", "6.4km return, 3h, Gr 4, rainforest, strangler figs, west views"},
  {14, "Cypress Grove Circuit", "cypress grove", "2.5km return, 1.5h, Gr 3, cypress pines, birds"},

  // ---- Mount Nebo (D'Aguilar, Jollys Lookout) ----
  {15, "Egernia Circuit", "egernia", "1.5km circuit, 45min, Gr 3, forest, near Jollys Lookout"},
  {15, "Thylogale Track", "thylogale", "8km return, 3h, Gr 3, forest, Jollys Lookout start"},

  // ---- The Gap (D'Aguilar, Walkabout Creek) ----
  {16, "Araucaria Track", "araucaria", "5km return, 2h, Gr 3, rainforest"},
  {16, "Enoggera Reservoir Circuit", "enoggera reservoir|enoggera", "shoreline loop, ~1.5h, ungraded, reservoir, birdlife"},

  // ---- Glass House Mountains ----
  {17, "Mount Ngungun Summit Walk", "ngungun|mt ngungun|mount ngungun", "2.8km return, 2h, Gr 4, 360 views, cliff edges"},
  {17, "Tibrogargan Circuit", "tibrogargan", "4.1km return, 1.5h, Gr 3, mountain views, woodland"},
  {17, "Trachyte Circuit", "trachyte", "5.7km return, 2-3h, Gr 3, woodland, heathland"},
  {17, "Mount Beerburrum Walk", "beerburrum|mt beerburrum|mount beerburrum", "3.5km return, 1.5h, Gr 3-4, fire tower, views"},
  {17, "Yul-yan-man Track", "yulyanman", "9km return, 3-4h, Gr 5, rock scramble, views"},

  // ---- Noosa (Noosa National Park) ----
  {18, "Noosa Coastal Walk", "coastal walk|noosa coastal|hells gates|fairy pools|tea tree bay", "10.8km return, 4h, Gr 4, beaches, Hell's Gates, Fairy Pools"},
  {18, "Tanglewood Walk", "tanglewood", "8km return, 2-3h, Gr 4, forest, ends near Hell's Gates"},
  {18, "Noosa Hill Walk", "noosa hill", "2.8km return, 1-1.5h, Gr 4, forest climb"},
  {18, "Noosa Palm Grove Walk", "noosa palm grove", "1.1km return, 15-30min, Gr 3, rainforest"},
  {18, "Alexandria Bay Walk", "alexandria bay", "4.2km return, 1h, Gr 4, beach, headland"},
  {18, "Emu Mountain Summit Walk", "emu mountain|emu mountain summit", "1.1km return, 45-60min, Gr 4, coastal views"},

  // ---- Kondalilla ----
  {19, "Kondalilla Falls Circuit", "kondalilla falls", "4.7km circuit, 2h, Gr 3, 90m waterfall, 300+ steps"},
  {19, "Picnic Creek Circuit", "picnic creek|picnic creek falls", "1.7km return, 45min, Gr 2, Picnic Creek Falls"},

  // ---- Mapleton ----
  {20, "Wompoo Circuit", "mapleton falls|wompoo", "1.3km return, 45min, Gr 2, falls lookout, rainforest"},

  // ---- Conondale National Park ----
  {21, "Booloumba Falls Walk", "booloumba falls|booloumba creek falls", "3km return, 1.5h, Gr 3, cascades, rock pools"},
  {21, "Mount Allan Track", "mount allan|mt allan", "11km return, ~3.5h, Gr 4, fire tower, Mary Valley views"},

  // ---- Coolum ----
  {22, "Mount Coolum Summit Walk", "mount coolum|mt coolum", "1.6km return, 1-2h, Gr 4, steep, coastal views"},

  // ---- Pomona ----
  {23, "Mount Cooroora Climb", "mount cooroora|mt cooroora|cooroora", "2km return, ~2h, ungraded, steep climb, views"},

  // ---- North Stradbroke Island ----
  {24, "North Gorge Walk", "gorge walk|north gorge|point lookout gorge walk", "1.5km circuit, 45min, Gr 2, headlands, turtles, whales"},
  {24, "Blue Lake Track", "blue lake|kaboora track|kaboora", "5.2km return, 1.5h, ungraded, freshwater lake, bush"},

  // ---- Moreton Island ----
  {25, "Mount Tempest Lookout Track", "mount tempest|mt tempest|tempest", "2.5km return, 2h, steep (Parks: difficult), dune views"},

  // ---- Toohey Forest (Brisbane, Tarragindi) ----
  {26, "Toohey Ridge Track", "toohey ridge", "3km one way, 1h, easy, paved, wheelchair and pram friendly"},
  {26, "Toohey Mountain Track", "toohey mountain", "1.5km one way, 50min, easy, sandstone, Pegg's Outlook"},

  // ---- Bunyaville (Brisbane) ----
  {27, "Bunyaville Circuit", "bunyaville", "1.9km circuit, ~45min, easy, gentle bush, koala signs"},

  // ---- Maleny ----
  {28, "Mary Cairncross Forest Walk", "mary cairncross|mary cairncross scenic reserve", "2km loop, 1.25h, Gr 2, rainforest, Glass House views"},
  {28, "Gardners Falls", "gardeners falls", "short walk, 10min, Gr 1, rock pool, swimming"},

  // ---- Buderim ----
  {29, "Buderim Forest Waterfall Walk", "serenity falls|buderim falls|buderim forest", "short walk, 15-30min, Gr 1, waterfall, rainforest"},

  // ---- Kenilworth (Mary Valley) ----
  {30, "Fig Tree Walk", "fig tree|imbil fig tree walk", "1km circuit, 45min, Gr 1, giant figs, wheelchair friendly"},

  // ---- Bribie Island ----
  {31, "Banksia Bicentennial Walk", "banksia walk|banksia bicentennial", "0.5km one way, 15min, easy, wallum heath, wildlife"},
  {31, "Palm Grove Bicentennial Walk", "bribie palm grove", "1.3km return, 1h, easy, fan palms, koalas"},
  {31, "Melaleuca Bicentennial Walk", "melaleuca walk|bribie melaleuca", "1.5km return, 1.25h, easy, paperbark wetland, birds"},

  // ---- Flinders-Goolman Conservation Estate (Ipswich, Peak Crossing) ----
  {32, "Flinders Peak Summit", "flinders peak|mount flinders|mt flinders", "6.5km return, 3.5h, Gr 5, scrambling, 360 views"},
  {32, "Mount Goolman Summit", "mount goolman|mt goolman|goolman", "4.5km return, ~3h, Gr 4, rock scramble, 360 views"},
  {32, "Mount Blaine Track", "mount blaine|mt blaine|blaine", "5.6km return, ~2.5h, Gr 4, off-track bits, views"},

  // ---- Mount Mee (D'Aguilar National Park north) ----
  {33, "Somerset Trail", "somerset lookout", "4h, Gr 4, Somerset Dam views, unfenced cliffs"},
  {33, "Lophostemon Walk", "lophostemon", "20min, Gr 3, forest, Neurum Creek waterholes"},
  {33, "Falls Lookout Track", "falls lookout|bulls falls", "0.5km one way, ungraded, Neurum Valley views"},

  // ---- Numinbah Valley (Neranwood, Upper Nerang) ----
  {34, "Pages Pinnacle", "pages|pages pinnacle track", "6.7km return, 2.5h, ungraded, steep fire trail, razorback scramble"},
  {34, "Bohgaban Falls", "boghaban falls|boggaban falls|bohgaban", "~4km return from Great Walk, 2-3h, ungraded, off-track rock hopping"},
};

#define TRAIL_REGION_COUNT ((int)(sizeof(TRAIL_REGIONS) / sizeof(TRAIL_REGIONS[0])))
#define TRAIL_TOWN_COUNT ((int)(sizeof(TRAIL_TOWNS) / sizeof(TRAIL_TOWNS[0])))
#define TRAIL_COUNT      ((int)(sizeof(TRAILS) / sizeof(TRAILS[0])))
