#pragma once
/*
 * Trail library for the trail info bot (item 32): Gold Coast and Scenic Rim, Queensland.
 *
 * Compiled into the firmware (flash). To change a trail, edit this file and rebuild.
 *
 * Rules (tools/check_trails.py checks them):
 *   - Names, aliases and info are plain ASCII (an apostrophe in a name is fine).
 *   - 'aliases' are other names people type, lowercase, separated by '|'. No apostrophes, no punctuation.
 *   - 'info' is "<distance>, <time>, <grade>, <features>". "<name>: <info>" must be 110 characters or fewer.
 *   - No alias may be used by two different trails, or by two different towns.
 *   - A trail name or alias that is also a town name wins: "!natural bridge" gives the trail.
 *   - Grades are the Queensland Parks 1 to 5 scale. "ungraded" means there is no official grade.
 *   - Facts come from Queensland Parks and Wildlife Service pages, the City of Gold Coast, and hiker logs
 *     where no official figure exists (those rows say "~" or "off-track"). CHECK PARK ALERTS BEFORE WALKING.
 */

#include <stdint.h>

struct TrailTown {
  const char* name;      // shown in lists
  const char* aliases;   // '|' separated, lowercase
};

struct Trail {
  uint8_t town;          // index into TRAIL_TOWNS
  const char* name;      // shown in lists and as the start of the reply
  const char* aliases;   // '|' separated, lowercase
  const char* info;      // the rest of the reply
};

static const TrailTown TRAIL_TOWNS[] = {
  /* 0  */ {"Springbrook", "springbrook np|springbrook national park"},
  /* 1  */ {"Natural Bridge", ""},
  /* 2  */ {"Mount Cougal", "mt cougal|cougal|currumbin valley"},
  /* 3  */ {"Tamborine Mountain", "tamborine|mt tamborine|mount tamborine|tamborine np|tamborine national park"},
  /* 4  */ {"O'Reilly's", "green mountains"},
  /* 5  */ {"Binna Burra", "binna"},
  /* 6  */ {"Mount Barney", "mt barney|barney"},
  /* 7  */ {"Burleigh Heads", "burleigh|burleigh head"},
  /* 8  */ {"Tallebudgera Valley", "tallebudgera"},
  /* 9  */ {"Wongawallan", ""},
  /* 10 */ {"Bonogin", ""},
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

  // ---- Natural Bridge ----
  {1, "Natural Bridge Circuit", "natural bridge|natural arch|glow worm cave|glowworm cave", "1km circuit, 1h, Gr 2, cave, waterfall"},

  // ---- Mount Cougal ----
  {2, "Cougal Cascades Track", "cougal cascades|cascades", "1.6km return, 1h, Gr 2, cascades, old sawmill"},
  {2, "Mount Cougal Summit", "cougal summit|cougal east|east cougal", "~10km return, 5h, ungraded, off-track, hard navigation, views"},
  {2, "Boyd's Butte", "boyds view", "~8km return, 4h, ungraded, off-track, exposed top, views"},
  {2, "Cougal Boyds Circuit", "boyds butte circuit|cascades boyds circuit", "14.5km circuit, 6h, ungraded, off-track, easy to lose trail"},

  // ---- Tamborine Mountain ----
  {3, "Curtis Falls Track", "curtis falls|curtis waterfall", "1.1km return, 30min, Gr 3, waterfall, rainforest"},
  {3, "Lower Creek Circuit", "lower creek", "2.5km return, 1h, Gr 4, creek, strangler fig"},
  {3, "Cedar Creek Falls Lookout", "cedar creek falls|cedar creek lookout", "0.5km return, 15min, Gr 1, waterfall, lookout"},
  {3, "Cedar Creek Rock Pools", "cedar creek falls rock pools", "1.1km return, 30min, Gr 3, rock pools, creek"},
  {3, "MacDonald Rainforest Circuit", "macdonald circuit", "1.4km return, 30min, Gr 3, strangler figs, palms"},
  {3, "Palm Grove Circuit", "palm grove", "2.7km circuit, 1h, Gr 4, palms, rainforest"},
  {3, "Jenyns Circuit", "jenyns", "4.8km circuit, 1.5h, Gr 4, rainforest, eucalypt forest"},
  {3, "Witches Falls Circuit", "witches falls", "3.6km return, 1h, Gr 4, waterfall, lagoons"},
  {3, "Witches Chase Track", "witches chase", "2.6km return, 1h, Gr 4, red cedars, lookout"},
  {3, "Sandy Creek Circuit", "sandy creek|cameron falls", "2.6km circuit, 1h, Gr 4, creek, Cameron Falls lookout"},

  // ---- O'Reilly's (Green Mountains, Lamington) ----
  {4, "Centenary Track", "centenary", "1.8km return, 30min, Gr 1, sealed, accessible"},
  {4, "Python Rock Track", "python rock", "3.1km return, 1h, Gr 3, lookout, rainforest"},
  {4, "Moran's Falls Track", "morans falls", "4.4km return, 1.5h, Gr 4, 80m waterfall, gorge"},
  {4, "Rainforest Return", "rainforest return track", "1.4km circuit, 30min, Gr 2, rainforest"},
  {4, "Box Forest Circuit", "box forest", "10.9km return, 4h, Gr 4, waterfalls, brush box"},

  // ---- Binna Burra (Lamington) ----
  {5, "Binna Burra Rainforest Circuit", "binna burra rainforest|binna rainforest circuit", "1.2km circuit, 30min, Gr 2, rainforest"},
  {5, "Tullawallal Circuit", "tullawallal", "5km circuit, 1.5h, Gr 3, rainforest"},
  {5, "Caves Track", "caves|binna caves", "7km return, 2h, Gr 4, caves, lookout"},
  {5, "Gwongoorool Track", "gwongoorool", "6km return, 2h, Gr 4, pool, rainforest"},
  {5, "Daves Creek Circuit", "daves creek", "12km circuit, 4h, Gr 4, lookout, rainforest"},
  {5, "Border Track", "border", "21.4km one way, 7h, Gr 4, ridgelines, rainforest"},
  {5, "Ships Stern Circuit", "ships stern", "21km circuit, 8h, Gr 4, lookouts, Nagarigoon Falls"},

  // ---- Mount Barney ----
  {6, "Yellow Pinch Lookout", "yellow pinch", "2km return, ~1h, Gr 4, Barney views"},
  {6, "Cronan Creek Falls", "cronan creek", "12km return, 4h, Gr 4, creek, waterfall"},
  {6, "Lower Portals", "lower portals track", "7.4km return, ~3h, Gr 4, gorge, deep pool"},
  {6, "Upper Portals", "upper portals track", "8km return, time unknown, Gr 4, gorge, 4WD access"},

  // ---- Burleigh Heads ----
  {7, "Oceanview Track", "oceanview|oceanview walk|ocean view", "1.2km one way, 30min, Gr 2, coast, basalt columns"},
  {7, "Burleigh Rainforest Circuit", "burleigh rainforest|tumgun lookout|tumgun", "2.3km return, 45min, Gr 3, rainforest, Tumgun lookout"},

  // ---- Tallebudgera Valley ----
  {8, "Gorge Falls", "tallebudgera gorge falls", "4-5.6km return, 2h, ungraded, creek crossings, swimming"},

  // ---- Wongawallan ----
  {9, "Mount Wongawallan Track", "mount wongawallan|mt wongawallan", "1.5km return, 30min, ungraded, steep, lookout"},

  // ---- Bonogin ----
  {10, "Bally Mountain Summit Track", "bally mountain|bally mtn", "6km return, 3-4h, Gr 5, rock scramble, views"},
};

#define TRAIL_TOWN_COUNT ((int)(sizeof(TRAIL_TOWNS) / sizeof(TRAIL_TOWNS[0])))
#define TRAIL_COUNT      ((int)(sizeof(TRAILS) / sizeof(TRAILS[0])))
