#pragma once
/*
 * Wildlife library for the hike bot: local animals, plants and a short field guide for South East Queensland.
 *
 * Compiled into the firmware (flash, all const: no RAM). To change an entry, edit this file and rebuild.
 * Lookups are in WildBot.h; tools/check_wildlife.py checks the rules below.
 *
 * Rules:
 *   - Plain ASCII. No double quotes or backslashes. An apostrophe in a name is fine.
 *   - 'aliases' are other names people type: lowercase, no apostrophes or punctuation, separated by '|'.
 *   - 'info' is the reply after "<name>: ". Up to 4 messages separated by '~'. Each message is at most 110 characters,
 *     and the first one is counted with the name: "<name>: <first part>" must be 110 characters or fewer.
 *   - No name or alias may belong to two species, two categories, two groups, or to a trail, town or region in trails.h.
 *   - Lookup order for "!<word>": trail, town, region, species, category, group.
 *   - A category lists its species (keep a category to 24 species or fewer; a list is at most 4 messages).
 *   - In a sentence the bot finds a species by its name or any alias (with or without a plural s). Generic one-word
 *     aliases would trigger on ordinary chat, so keep them for exact "!<word>" use only when truly unambiguous.
 *   - Snake, spider, tick and leech sentences are caught by the safety rules first (chatrules.h), so those entries are
 *     reached with an exact command such as "!eastern brown snake". Emergencies always win.
 *   - Facts are general and local: check Queensland Parks and Wildlife, Queensland Museum, Queensland Health and the
 *     Atlas of Living Australia before changing safety, protected-species or phone-number text.
 */

#include <stdint.h>

struct WildGroup {
  const char* name;      // shown in lists
  const char* aliases;   // '|' separated, lowercase
  int8_t parent;         // index of the parent group, -1 = top level (listed by !wildlife)
};

struct WildCat {
  const char* name;      // shown in lists
  const char* aliases;   // '|' separated, lowercase
  uint8_t group;         // index into WILD_GROUPS
};

struct Wild {
  uint8_t cat;           // index into WILD_CATS
  const char* name;      // shown in lists and as the start of the reply
  const char* aliases;   // '|' separated, lowercase
  const char* info;      // the rest of the reply; '~' starts a new message
};

static const WildGroup WILD_GROUPS[] = {
  {"Animals", "animals|animal|fauna|native animals|local animals", -1},
  {"Birds", "birds|bird|birdlife|birding|birdwatching", 0},
  {"Plants", "plants|plant|flora|trees|native plants|local plants|vegetation", -1},
  {"Field guide", "field guide|fieldguide|nature guide|wildlife guide", -1},
};
#define WILD_GROUP_COUNT ((int)(sizeof(WILD_GROUPS) / sizeof(WILD_GROUPS[0])))

static const WildCat WILD_CATS[] = {
  {"Mammals", "mammals|mammal|marsupials|marsupial", 0},   // 0
  {"Bats", "bats|bat|flying foxes|flying fox", 0},   // 1
  {"Forest birds", "forest birds|rainforest birds|garden birds|bush birds|songbirds", 1},   // 2
  {"Parrots and pigeons", "parrots|parrot|cockatoos|pigeons|doves|rosellas", 1},   // 3
  {"Raptors and owls", "raptors|raptor|owls|owl|eagles|hawks|kites|birds of prey|night birds", 1},   // 4
  {"Water birds", "water birds|waterbirds|shorebirds|shore birds|seabirds|sea birds|waders|coastal birds|ducks", 1},   // 5
  {"Snake types", "snake types|snake species|types of snakes|snake list|list of snakes", 0},   // 6
  {"Lizards and turtles", "lizards|lizard|reptiles|reptile|goannas|turtles|turtle|skinks", 0},   // 7
  {"Frogs", "frogs|frog|amphibians", 0},   // 8
  {"Spider types", "spider types|spider species|types of spiders|arachnids", 0},   // 9
  {"Insects", "insects|insect|bugs|butterflies|butterfly|beetles", 0},   // 10
  {"Stingers and biters", "stingers|biters|bites and stings|venomous animals|biting animals|stinging animals", 0},   // 11
  {"Freshwater life", "freshwater life|freshwater animals|freshwater|fish|river animals|creek animals", 0},   // 12
  {"Marine life", "marine life|marine animals|sea life|ocean life|marine|coastal animals", 0},   // 13
  {"Feral animals", "feral animals|feral|introduced animals|pest animals|invasive animals", 0},   // 14
  {"Dangerous plants", "dangerous plants|poisonous plants|toxic plants|stinging plants|plants to avoid", 2},   // 15
  {"Rainforest trees", "rainforest trees|rainforest plants|brush trees", 2},   // 16
  {"Eucalypts and forest trees", "eucalypts|eucalyptus|gum trees|gums|forest trees", 2},   // 17
  {"Coastal and heath plants", "coastal plants|heath plants|heath|beach plants|coastal and heath", 2},   // 18
  {"Wildflowers", "wildflowers|wildflower|flowers|orchids|wattles", 2},   // 19
  {"Ferns and palms", "ferns|fern|palm trees|ferns and palms", 2},   // 20
  {"Weeds", "weeds|weed|invasive plants|pest plants|noxious weeds", 2},   // 21
  {"Wildlife calendar", "wildlife calendar|nature calendar|seasons|wildlife seasons", 3},   // 22
  {"Tracks and signs", "tracks and signs|animal tracks|footprints|scats|animal signs", 3},   // 23
  {"Wildlife care", "wildlife care|wildlife rules|wildlife help|caring for wildlife", 3},   // 24
};
#define WILD_CAT_COUNT ((int)(sizeof(WILD_CATS) / sizeof(WILD_CATS[0])))

static const Wild WILDLIFE[] = {
  // ---- Mammals
  {0, "Koala", "koalas|koala bear", "Grey, thick-furred, and asleep in a gum most of the day. Give it space and keep dogs on a lead."},
  {0, "Eastern grey kangaroo", "grey kangaroo|grey kangaroos|eastern grey|kangaroo|kangaroos|roo|roos", "Big grey roo in groups on grassy forest edges at dawn and dusk. Keep your distance."},
  {0, "Red-necked wallaby", "rednecked wallaby|red necked wallaby|bennetts wallaby", "Sandy grey with a reddish neck and black paws. Grazes at dusk on open forest edges."},
  {0, "Swamp wallaby", "black wallaby|swamp wallabies", "Dark, shaggy wallaby that crouches low. Lives in thick undergrowth and heath at dusk."},
  {0, "Red-legged pademelon", "pademelon|pademelons|red legged pademelon", "Small, stocky, rufous wallaby of rainforest edges. Shy; feeds at dawn and dusk."},
  {0, "Short-beaked echidna", "echidna|echidnas|spiny anteater|short beaked echidna", "Spiny and slow, digs for ants and termites. Watch from a distance and let it be."},
  {0, "Platypus", "platypuses|platypi", "Shy duck-billed swimmer of clean creeks, seen at dawn and dusk. Males have venomous spurs."},
  {0, "Common brushtail possum", "brushtail possum|brushtail|possum|possums", "Grey with a bushy tail, noisy at night. Shelters in hollows. Please don't feed it."},
  {0, "Common ringtail possum", "ringtail possum|ringtail|ringtail possums", "Small possum with a white-tipped tail it grips with. Builds ball nests in shrubs."},
  {0, "Sugar glider", "sugar gliders", "Tiny glider with a dark back stripe. Glides between trees at night in family groups."},
  {0, "Squirrel glider", "squirrel gliders", "Bigger than a sugar glider, with a bushy tail. Glides between hollow trees at night."},
  {0, "Greater glider", "greater gliders", "Big, fluffy, nocturnal glider that eats gum leaves and needs old hollow trees. Threatened."},
  {0, "Rufous bettong", "rufous bettongs|bettong|bettongs", "Small kangaroo relative that carries nest grass with its tail. Shy and nocturnal."},
  {0, "Long-nosed bandicoot", "long nosed bandicoot|bandicoot|bandicoots", "Forages at night, leaving small cone-shaped holes in lawns and leaf litter."},
  {0, "Rakali", "water rat|water rats|rakalis", "Sleek native water rat with a white-tipped tail. Swims in creeks and estuaries, often at dusk."},
  {0, "Spotted-tailed quoll", "spotted tailed quoll|quoll|quolls|tiger quoll", "Spotted coat and tail. A top predator of wet forests; threatened and very rarely seen."},
  {0, "Dingo", "dingoes|dingos|wild dog|wild dogs", "Tawny native dog, mostly shy. Don't feed or approach it; keep children close and dogs on a lead."},
  {0, "Antechinus", "antechinuses|yellow footed antechinus", "Small, mouse-like marsupial that hunts insects. Males die after a frantic winter breeding season."},
  // ---- Bats
  {1, "Grey-headed flying fox", "grey headed flying fox|grey headed flying foxes", "Big fruit and blossom bat with a rusty collar. Roosts in noisy camps. Vulnerable."},
  {1, "Black flying fox", "black flying foxes", "All-dark fruit bat. Roosts in big camps by day and pollinates native trees at night."},
  {1, "Little red flying fox", "little red flying foxes", "Smallest local flying fox. Nomadic, follows flowering trees and gathers in huge camps."},
  {1, "Microbats", "microbat|insect bats|small bats", "Tiny insect-eating bats that hunt at dusk. Roost in tree hollows, caves, and under bark."},
  {1, "Bat bite or scratch", "bat bite|bat scratch|bat safety|lyssavirus|ablv|bat bitten", "Never touch a bat. If bitten or scratched, wash gently with soap and water for 5 minutes.~Then get urgent medical help. Bat lyssavirus is rare but can be fatal."},
  {1, "Flying fox camps", "flying fox camp|bat camp|bat camps", "Camps are loud and smelly. Keep dogs away. Never touch a fallen bat; call 1300 264 625."},
  // ---- Forest birds
  {2, "Australian brush-turkey", "brush turkey|bush turkey|scrub turkey|brushturkey", "Black bird, red head, yellow wattle. Males build big leaf-litter mounds for the eggs."},
  {2, "Albert's lyrebird", "alberts lyrebird|lyrebird|lyrebirds|albert lyrebird", "Shy ground bird of cool rainforest and a brilliant mimic. Listen for it on winter mornings."},
  {2, "Regent bowerbird", "regent bowerbirds", "Male is black with golden-yellow patches. Builds a twig avenue bower to impress females."},
  {2, "Satin bowerbird", "satin bowerbirds", "Male is glossy blue-black with violet eyes and decorates his bower with blue objects."},
  {2, "Noisy pitta", "noisy pittas", "Colourful rainforest ground bird that hops through leaf litter. Calls walk-to-work."},
  {2, "Eastern whipbird", "whipbird|whipbirds", "Heard far more than seen. The male's whip-crack call is answered by the female's choo-choo."},
  {2, "Eastern yellow robin", "yellow robin|yellow robins", "Bright yellow belly. Clings sideways to tree trunks and often follows walkers."},
  {2, "Variegated fairy-wren", "variegated fairy wren|fairy wren|fairy wrens|fairywren|blue wren", "Tiny, with a cocked tail. The breeding male is bright blue with a chestnut shoulder."},
  {2, "Eastern spinebill", "eastern spinebills|spinebill", "Small honeyeater with a long curved bill. Hovers at banksia and bottlebrush flowers."},
  {2, "Bell miner", "bell miners|bellbird|bellbirds", "Olive-green honeyeater with a bell-like tink. Colonies farm sap-sucking insects."},
  {2, "Noisy miner", "noisy miners", "Grey honeyeater with a yellow patch behind the eye. Aggressively mobs other birds."},
  {2, "Spangled drongo", "drongo|drongos|spangled drongos", "Glossy black with a forked tail and red eye. Fearless and a clever mimic."},
  {2, "Willie wagtail", "willy wagtail|wagtail|willie wagtails", "Black and white. Fans its tail while perched; bold, chatty, and chases larger birds."},
  {2, "Rufous fantail", "rufous fantails", "Small rufous bird that fans its tail while flitting. A summer breeding visitor to forests."},
  {2, "Golden whistler", "golden whistlers", "Male is bright yellow with a black throat band and a loud, ringing whistle."},
  {2, "Figbird", "figbirds", "Fruit-eating bird. Male has a red face patch; female is streaky brown. Flocks in fruiting figs."},
  {2, "Pied currawong", "currawong|currawongs|pied currawongs", "Black and white with a yellow eye and a ringing curra-wong call. Moves downhill in winter."},
  {2, "Australian magpie", "magpie|magpies|australian magpies", "Black and white with a carolling song. Males swoop in spring: walk away, don't run."},
  {2, "Channel-billed cuckoo", "channel billed cuckoo|channel billed cuckoos", "Huge summer visitor with a massive bill and a raucous call over the treetops."},
  {2, "Pacific koel", "koel|koels|pacific koels", "Summer visitor. Male is glossy black with a red eye and a rising coo-ee all night."},
  // ---- Parrots and pigeons
  {3, "Rainbow lorikeet", "rainbow lorikeets|lorikeet|lorikeets", "Blue head, green back, orange-red chest. Noisy flocks in flowering trees. Don't feed."},
  {3, "Scaly-breasted lorikeet", "scaly breasted lorikeet|scaly breasted lorikeets", "Green with a scaly yellow chest. Often feeds quietly in flowering gums."},
  {3, "Australian king-parrot", "king parrot|king parrots|australian king parrot", "Male has a red head and chest and a green back. Visits rainforest edges and gardens."},
  {3, "Crimson rosella", "crimson rosellas", "Red body with blue cheeks and wings. Common in cooler rainforest and eucalypt forest."},
  {3, "Pale-headed rosella", "pale headed rosella|pale headed rosellas", "Cream head, blue cheeks, scalloped back. Usually seen in pairs in open woodland."},
  {3, "Galah", "galahs", "Pink and grey cockatoo that lives in noisy flocks in grassland and open woodland."},
  {3, "Sulphur-crested cockatoo", "sulphur crested cockatoo|cockatoo|white cockatoo", "Loud white parrot with a yellow crest. Chews timber and gear; please don't feed it."},
  {3, "Yellow-tailed black-cockatoo", "yellow tailed black cockatoo|yellow tailed black cockatoos|black cockatoo|black cockatoos", "Large dark cockatoo with yellow tail panels. Listen for its wailing flight call."},
  {3, "Wompoo fruit-dove", "wompoo fruit dove|wompoo pigeon|fruit dove|fruit doves", "Large green, purple and yellow pigeon of the rainforest canopy. Deep wollack-a-woo call."},
  {3, "Topknot pigeon", "topknot pigeons", "Grey pigeon with a rufous crest. Flies in flocks and follows fruiting rainforest trees."},
  {3, "Wonga pigeon", "wonga pigeons", "Plump grey pigeon with a white face. Feeds on the forest floor; repetitive ringing call."},
  {3, "Crested pigeon", "crested pigeons", "Grey-brown pigeon with a thin upright crest. Its wings whistle on take-off."},
  // ---- Raptors and owls
  {4, "Laughing kookaburra", "kookaburra|kookaburras|laughing kookaburras", "Big kingfisher whose laugh marks dawn and dusk. Eats insects, lizards and small snakes."},
  {4, "Powerful owl", "powerful owls", "Australia's largest owl. Hunts possums and gliders in wet forest. Listen for a deep woo-hoo."},
  {4, "Southern boobook", "boobook|boobook owl|mopoke|southern boobooks", "Small brown owl whose boo-book call is often heard at night."},
  {4, "Tawny frogmouth", "frogmouth|frogmouths|tawny frogmouths", "Owl-like, but not an owl. Looks like a broken branch by day and hunts insects at night."},
  {4, "Eastern barn owl", "barn owl|barn owls", "Pale, heart-shaped face and an eerie screech. Hunts rodents at night in open country."},
  {4, "Bush stone-curlew", "bush stone curlew|stone curlew|stone curlews", "Ground bird with a wailing night call that freezes when disturbed. Keep dogs away."},
  {4, "White-bellied sea-eagle", "white bellied sea eagle|sea eagle|sea eagles", "Big white and grey raptor that nests in tall trees by bays and rivers. Eats fish."},
  {4, "Wedge-tailed eagle", "wedge tailed eagle|wedgetail|wedgetails|wedgie", "Huge dark eagle with a long wedge-shaped tail. Often soars on thermals over the ranges."},
  {4, "Whistling kite", "whistling kites", "Pale brown raptor with a descending whistle. Often circles near water."},
  {4, "Brahminy kite", "brahminy kites", "Chestnut body with a white head and chest. Scavenges along coasts and estuaries."},
  {4, "Black-shouldered kite", "black shouldered kite|black shouldered kites", "Small white raptor that hovers over grassland hunting mice."},
  {4, "Peregrine falcon", "peregrine|peregrines|peregrine falcons", "The fastest bird. Nests on cliffs and tall buildings and hunts birds in a high-speed dive."},
  {4, "Nankeen kestrel", "kestrel|kestrels|nankeen kestrels", "Small rufous falcon that hovers in open country scanning for prey."},
  {4, "Osprey", "ospreys", "Pale-headed raptor that plunges feet-first for fish. Builds big stick nests on poles and islands."},
  {4, "Pacific baza", "baza|pacific bazas", "Crested hawk with a barred chest. Hunts large insects and tree frogs in the canopy."},
  // ---- Water birds
  {5, "Australian pelican", "pelican|pelicans|australian pelicans", "Huge white bird with the longest bill of any bird. Scoops fish in bays and lakes."},
  {5, "Australian white ibis", "white ibis|ibis|bin chicken|bin chickens", "White with a bare black head and curved bill. Scavenges in parks; please don't feed it."},
  {5, "Straw-necked ibis", "straw necked ibis", "Black and white with golden straw-like neck plumes. Flocks over paddocks eating insects."},
  {5, "Eastern curlew", "curlew|curlews|eastern curlews", "World's largest curlew. Critically endangered migrant in Moreton Bay in summer. Keep clear."},
  {5, "Bar-tailed godwit", "bar tailed godwit|godwit|godwits", "Long-legged shorebird that flies non-stop from Alaska to Australia and arrives each spring."},
  {5, "Pied oystercatcher", "oystercatcher|oystercatchers|pied oystercatchers", "Black and white with a long orange-red bill. Prises open shellfish on beaches and flats."},
  {5, "Great egret", "great egrets|egret|egrets", "Tall white heron with a yellow bill. Stalks fish and frogs in shallow water."},
  {5, "Little egret", "little egrets", "Small white heron with a black bill and yellow feet. Stirs the mud with its feet to flush prey."},
  {5, "White-faced heron", "white faced heron|white faced herons", "Blue-grey heron with a white face. Common on creeks, mudflats, and even lawns."},
  {5, "Black swan", "black swans", "Black body, red bill, and white wing tips. Found on lakes and wetlands."},
  {5, "Pacific black duck", "black duck|black ducks|pacific black ducks", "Common dark brown duck with a striped face. Found on almost any water."},
  {5, "Australasian darter", "darter|darters|snake bird|snakebird|snakebirds", "Long-necked snake bird that swims with only its neck above water and spears fish."},
  {5, "Little pied cormorant", "little pied cormorants|cormorant|cormorants", "Small black and white diving bird. Dries its wings spread wide on a perch."},
  {5, "Silver gull", "seagull|seagulls|gull|gulls|silver gulls", "White gull with a red bill and legs. Scavenges for chips, so please don't feed it."},
  {5, "Crested tern", "crested terns|tern|terns", "Grey and white seabird with a shaggy black cap. Dives for fish along the coast."},
  {5, "Masked lapwing", "plover|plovers|masked plover|masked lapwings|lapwing", "Yellow face wattles and a loud alarm call. Swoops when nesting; walk away calmly."},
  {5, "Comb-crested jacana", "comb crested jacana|jacana|jacanas|lotus bird|lotusbird", "Walks on floating lily pads on very long toes. A wetland specialist."},
  {5, "Purple swamphen", "swamphen|swamphens|pukeko|purple swamphens", "Blue-purple with a red bill and frontal shield. Stalks wetland edges eating reeds."},
  {5, "Azure kingfisher", "azure kingfishers", "Tiny and brilliant blue above, orange below. Fishes from low perches over creeks."},
  {5, "Sacred kingfisher", "sacred kingfishers", "Turquoise above, buff below. Perches in open forest, often far from water."},
  {5, "Beach stone-curlew", "beach stone curlew|beach stone curlews|beach thick knee", "Large-billed shorebird that nests on sand. Threatened; keep dogs off nesting beaches."},
  {5, "Australasian grebe", "grebe|grebes|dabchick|australasian grebes", "Small diving waterbird with a floating nest. Pops up far from where it dived."},
  // ---- Snake types
  {6, "Eastern brown snake", "eastern brown|brown snake|brown snakes|common brown snake", "Fast, slender, highly venomous. Back away. Bitten: firm bandage, keep still, call 000."},
  {6, "Red-bellied black snake", "red bellied black snake|red bellied black|black snake|redbelly", "Glossy black, red belly. Shy but venomous, often near water. Bitten: bandage, 000."},
  {6, "Coastal taipan", "taipan|taipans|coastal taipans", "Large, fast, highly venomous, narrow head. Rarely seen. Back away; bitten: bandage, 000."},
  {6, "Carpet python", "carpet pythons|=python|=pythons", "Large non-venomous constrictor with a patterned back. Can bite; leave it be."},
  {6, "Green tree snake", "common tree snake|tree snake|green tree snakes", "Slender, green or blue-grey with a yellow belly and big eyes. Harmless."},
  {6, "Brown tree snake", "boiga|cat eyed snake", "Nocturnal, big head and eyes. Mildly venomous, rear-fanged, rarely bites. Don't handle."},
  {6, "Keelback", "freshwater snake|keelbacks", "Harmless, rough-scaled snake near water. One of the few snakes that can eat cane toads and survive."},
  {6, "Rough-scaled snake", "rough scaled snake|rough scaled snakes", "Venomous, brown, keeled scales. Wet forest creeks; can be defensive. Keep clear."},
  {6, "Common death adder", "death adder|death adders|common death adders", "Short and stout with a triangular head. Ambushes from leaf litter. Watch your step."},
  {6, "Eastern small-eyed snake", "eastern small eyed snake|small eyed snake", "Small, glossy black, pink or cream belly. Venomous but rarely seen or bites."},
  {6, "Black-bellied swamp snake", "black bellied swamp snake|swamp snake", "Small, dark, venomous snake of swamps and creeks. Eats frogs. Don't handle."},
  {6, "Stephens's banded snake", "stephens banded snake|stephens banded|banded snake", "Black and cream bands, venomous, nocturnal, climbs in rocky forest. Give space."},
  {6, "Yellow-faced whip snake", "yellow faced whip snake|whip snake", "Slender and fast, with a yellow face stripe. Mildly venomous, not dangerous."},
  {6, "Snake bite first aid", "snake first aid|snakebite first aid|snake bite treatment|snake bite first aid", "Stay still. Firm bandage over the bite and up the whole limb, splint it, call 000.~Never cut, suck, wash or use a tourniquet on the bite."},
  {6, "Snake safety tips", "snake safety|snake safe|avoid snakes|snake tips", "Wear boots and long pants, watch your step, never reach under logs, give snakes room."},
  // ---- Lizards and turtles
  {7, "Lace monitor", "goanna|lace goanna|lace monitors", "Big, banded climbing lizard that flicks a forked tongue. Can scratch and bite. Don't feed it."},
  {7, "Eastern water dragon", "water dragon|water dragons|eastern water dragons", "Large lizard of creeks and parks. Basks on rocks and dives into water when startled."},
  {7, "Eastern bearded dragon", "bearded dragon|bearded dragons|beardie", "Stocky, spiny lizard that puffs out a dark beard to look bigger. Basks on logs."},
  {7, "Eastern blue-tongue", "eastern blue tongue|blue tongue|blue tongues|blue tongue lizard|bluey", "Stocky, slow lizard that hisses and flashes a blue tongue. Harmless to people."},
  {7, "Land mullet", "land mullets|major skink|major skinks", "Big glossy skink that crashes through leaf litter and shelters in logs. Harmless."},
  {7, "Angle-headed dragon", "angle headed dragon|angleheaded dragon|forest dragon", "Well-camouflaged rainforest dragon. Clings to tree trunks and holds very still."},
  {7, "Southern leaf-tailed gecko", "southern leaf tailed gecko|leaf tailed gecko|leaf tailed geckos", "Large, bark-patterned gecko of wet forest. Sits motionless on trunks at night."},
  {7, "Eastern water skink", "water skink|water skinks", "Shiny bronze skink with a pale side stripe. Basks by creeks and slips into the water."},
  {7, "Garden skink", "garden skinks", "Small, shiny brown skink that darts through leaf litter. Can drop its tail to escape."},
  {7, "Eastern snake-necked turtle", "eastern snake necked turtle|snake necked turtle|long necked turtle|eastern long necked turtle", "Dark shell and long neck. Smelly musk if handled. Often crosses roads in spring."},
  {7, "Saw-shelled turtle", "saw shelled turtle|sawshelled turtle", "Serrated back edge to its shell. Lives in rainforest and eucalypt creeks of SE Queensland."},
  {7, "Mary River turtle", "mary river turtles|green haired turtle|punk turtle", "Found only in the Mary River. Algae can grow on its head like green hair. Endangered."},
  // ---- Frogs
  {8, "Cane toad", "cane toads|toad|toads", "Introduced, warty, poison glands behind the eyes. Toxic to pets. Wash hands after touching."},
  {8, "Green tree frog", "green treefrog|whites tree frog|dumpy tree frog|green tree frogs", "Large, plump, smooth green frog with sticky toes. Often in gardens and drains. Harmless."},
  {8, "Striped marsh frog", "striped marsh frogs", "Striped brown frog with a tok call like a ball being hit. Foam nests on still water."},
  {8, "Eastern dwarf tree frog", "dwarf tree frog|eastern dwarf treefrog", "Small tree frog with an insect-like call. Common in coastal wetlands and gardens."},
  {8, "Peron's tree frog", "perons tree frog|perons treefrog", "Grey with emerald flecks and a loud rattling call. Visits gardens and ponds."},
  {8, "Great barred frog", "great barred frogs|barred frog", "Big brown frog, gold-flecked eyes, barred legs. Hunts in leaf litter by rainforest creeks."},
  {8, "Giant barred frog", "giant barred frogs", "Very large and endangered rainforest creek frog. Clean your boots and never handle frogs."},
  {8, "Fleay's barred frog", "fleays barred frog|fleays frog|fleays", "Endangered; lives in a few SE Queensland rainforest creeks. Clean boots stop chytrid."},
  {8, "Tusked frog", "tusked frogs", "Stout and toad-like with two tusks in its lower jaw. Lives under leaf litter near creeks."},
  {8, "Wallum sedgefrog", "wallum sedge frog|wallum sedgefrogs", "Tiny acid-water specialist of coastal wallum heath. Vulnerable. Stay on tracks."},
  {8, "Red-eyed tree frog", "red eyed tree frog|red eyed treefrog", "Bright green with red eyes and orange-red thighs. Calls high in wet forest after rain."},
  {8, "Chytrid and frogs", "chytrid|chytrid fungus|frog disease|frog hygiene|clean boots frogs", "Chytrid fungus kills frogs. Clean boots between creeks and don't move frogs or tadpoles."},
  // ---- Spider types
  {9, "Funnel-web spiders", "funnel web|funnel webs|funnelweb|funnel web spider|funnel web spiders|tree funnel web", "Big glossy black spider that can be deadly. Bitten: firm bandage, keep still, call 000."},
  {9, "Redback spider", "redback|redbacks|red back spider|redback spiders", "Small black spider with a red stripe in dry spots. Bite: ice pack, see a doctor; severe: 000."},
  {9, "White-tailed spider", "white tailed spider|white tail spider|white tailed spiders", "Grey-brown and cigar-shaped with a pale tail tip. Bites are painful but rarely serious."},
  {9, "Huntsman spider", "huntsman|huntsmen|huntsman spiders", "Large, flat, hairy and fast, but shy. Bites are rare and mild. Often rests behind bark."},
  {9, "Golden orb-weaving spider", "golden orb weaver|golden orb weavers|golden orb spider|orb weaver|orb weavers|golden orb", "Huge golden webs across tracks. The spider is harmless. Carry a stick."},
  {9, "St Andrew's cross spider", "st andrews cross spider|st andrews cross|st andrews spider", "Sits in a web with a zigzag of white silk and holds its legs in an X. Harmless."},
  {9, "Mouse spider", "mouse spiders", "Stocky burrower; males have red jaws. Bites are rare but are treated like a funnel-web bite."},
  {9, "Trapdoor spider", "trapdoor spiders|trap door spider|trapdoor", "Lives in a burrow with a hinged lid. Can bite if disturbed, so leave burrows alone."},
  {9, "Wolf spider", "wolf spiders", "Fast ground hunter that carries its babies on its back. Bite is painful but not dangerous."},
  // ---- Insects
  {10, "Richmond birdwing", "richmond birdwing butterfly|birdwing|birdwings", "SE Queensland's giant butterfly. Males are green and black. Larvae eat only a native vine."},
  {10, "Blue triangle", "blue triangle butterfly|blue triangle butterflies", "Fast, black butterfly with a row of pale blue triangles. Often along sunny paths."},
  {10, "Orchard swallowtail", "orchard butterfly|citrus butterfly", "Black with cream-yellow patches and a red spot. Its caterpillar eats citrus leaves."},
  {10, "Common crow butterfly", "crow butterfly|common crow", "Dark brown with white spots; flaps slowly. Caterpillars eat oleander and native figs."},
  {10, "Cicadas", "cicada|green grocer|greengrocer|black prince|double drummer", "Deafening summer buzzing, loudest in the heat of the day. Green Grocers and Black Princes are local."},
  {10, "Christmas beetle", "christmas beetles", "Shiny, metallic gold-brown beetles that swarm around lights in early summer."},
  {10, "Stick insects", "stick insect|phasmid|phasmids|leaf insect|leaf insects", "Twig mimics that sway like leaves in the breeze. Look closely, and handle gently if at all."},
  {10, "Praying mantis", "mantis|mantises|praying mantises", "Patient ambush hunter with spiny forelegs. It can turn its head to watch you."},
  {10, "Native stingless bee", "stingless bee|stingless bees|sugarbag|sugarbag bee|native bees|native bee", "Tiny black bees nesting in hollow logs. The honey is called sugarbag. They can't sting."},
  {10, "Dragonflies", "dragonfly|damselfly|damselflies", "Skim over creeks and ponds. Plenty of them often suggests clean water."},
  // ---- Stingers and biters
  {11, "Paralysis tick", "paralysis ticks|ixodes holocyclus|tick paralysis", "Lives in coastal forest and scrub, active in warm months. Check skin and hair after walks.~Freeze it, don't squeeze it: kill it with ether-based spray, no tweezers. Stuck or unwell: see a doctor."},
  {11, "Bull ant", "bull ants|bulldog ant|bulldog ants", "Large ant with big jaws and a painful sting. Avoid nests. Swelling or trouble breathing: 000."},
  {11, "Fire ants", "fire ant|red fire ant|red imported fire ant", "Small, aggressive, reddish-brown ants with burning stings. Report to 13 25 23; avoid nests."},
  {11, "Bees and wasps", "paper wasp|paper wasps|honey bee|honey bees|bees and wasp", "Leave nests alone. Scrape out stings. Swollen face or trouble breathing after a sting: 000."},
  {11, "Land leech", "land leeches|bush leech", "Found in damp forest after rain. Flick sideways with a fingernail. They drop off when full."},
  {11, "March fly", "march flies|horse fly|horsefly|horseflies", "Big flies with a painful bite in warm months near creeks. Repellent and long sleeves help."},
  {11, "Mosquitoes", "mosquito|mozzie|mozzies", "Can carry Ross River and Barmah Forest viruses. Use repellent and cover up around dusk."},
  {11, "Biting midges", "sandfly|sandflies|sand flies|midges|biting midge", "Sandflies bite at dawn and dusk near mangroves and beaches. Repellent and long sleeves help."},
  {11, "Giant centipede", "centipede|centipedes|giant centipedes", "Fast, orange-brown, with a very painful but rarely serious bite. Shake out boots and gear."},
  {11, "Scorpions", "scorpion|scorpion bite|scorpion sting", "Small, hide under bark and rocks. Stings hurt but aren't dangerous. Wear gloves when moving logs."},
  {11, "Stinging caterpillars", "caterpillar|caterpillars|hairy caterpillar|hairy caterpillars|stinging caterpillar", "Some hairy ones cause itchy rashes. Look, don't touch. Lift hairs with tape; don't rub."},
  // ---- Freshwater life
  {12, "Short-finned eel", "eel|eels|short finned eel", "Lives for years in creeks before migrating to the Coral Sea to breed."},
  {12, "Mary River cod", "mary river cod fish|=cod", "Endangered freshwater cod found naturally only in the Mary River. Never take or handle it."},
  {12, "Australian lungfish", "lungfish|queensland lungfish", "Ancient, large freshwater fish with a single lung. Protected; never touch it."},
  {12, "Lamington spiny crayfish", "spiny crayfish|crayfish|lamington crayfish", "Big blue-green crayfish of cool rainforest creeks. Needs clean water; don't collect."},
  {12, "Rainbowfish", "rainbow fish|rainbowfishes", "Small, colourful schooling fish in clear creeks. Males flash bright colours."},
  {12, "Bull shark", "bull sharks", "Uses tidal rivers, including Brisbane and Logan. Avoid swimming in murky tidal water."},
  // ---- Marine life
  {13, "Humpback whale", "humpback|humpbacks|humpback whales|whale|whales", "Passes the coast roughly June to November. Breaches and slaps; best seen from headlands."},
  {13, "Green turtle", "green turtles|green sea turtle|sea turtle|sea turtles", "Large turtle that grazes seagrass in Moreton Bay. Keep your distance and slow boats."},
  {13, "Loggerhead turtle", "loggerhead turtles|loggerhead", "Heavy-headed sea turtle that nests on beaches in warm months. Keep lights off beaches."},
  {13, "Dugong", "dugongs|sea cow", "Shy seagrass grazer in Moreton Bay. Keep boat speed down and give them room."},
  {13, "Bottlenose dolphin", "bottlenose dolphins|dolphin|dolphins", "Often seen near headlands and bays. Keep a respectful distance and never feed them."},
  {13, "Bluebottle", "bluebottles|blue bottle|blue bottles|portuguese man o war", "Washes ashore in onshore winds. Pick off tentacles, rinse with seawater, soak in hot water.~Severe pain or trouble breathing: call 000."},
  {13, "Jellyfish and stingers", "jellyfish|irukandji|box jellyfish|morbakka|marine stingers|marine stinger", "Stingers occur in warm months; swim at patrolled beaches. Severe pain: call 000."},
  {13, "Blue-ringed octopus", "blue ringed octopus", "Tiny but deadly, in rock pools; never touch. Bitten: firm bandage, CPR if needed, 000."},
  {13, "Cone shells", "cone shell|cone snail|cone snails", "Beautiful but venomous. Never pick up live shells. If stung, call 000."},
  {13, "Stingrays", "stingray|=rays", "Shuffle your feet in shallows to warn them off. Barb injuries need hospital care."},
  // ---- Feral animals
  {14, "Red fox", "red foxes|fox|foxes", "Introduced predator that threatens ground-nesting birds and small mammals. Report sightings."},
  {14, "Feral pig", "feral pigs|pig|pigs|wild pig|wild pigs", "Introduced and destructive. Can be aggressive. Keep clear and report sightings."},
  {14, "Feral cat", "feral cats|wild cat", "Major predator of native wildlife. Keep pet cats inside and don't feed strays."},
  {14, "Feral deer", "deer|rusa deer|fallow deer", "Introduced. Damage the forest understorey and cause vehicle crashes. Report sightings."},
  {14, "Common myna", "myna|mynah|indian myna|indian mynah|common mynah", "Introduced brown bird with a yellow face patch. Competes with natives for tree hollows."},
  {14, "European rabbit", "rabbit|rabbits|wild rabbit", "Introduced. Burrows cause erosion and the rabbits compete with native grazers."},
  // ---- Dangerous plants
  {15, "Stinging tree", "stinging trees|giant stinging tree|shiny leaved stinging tree|gympie gympie|dendrocnide", "Heart-shaped, hairy leaves cause severe pain, sometimes for weeks. Never touch or rub.~Lift hairs with wax strips or tape and get medical help."},
  {15, "Cunjevoi", "cunjevoi lily|giant lily|alocasia|native elephant ear", "Big glossy arrow-shaped leaves in damp forest. The sap irritates skin and mouth. Never eat any part."},
  {15, "Wait-a-while", "wait a while|lawyer vine|lawyer cane|calamus|rattan|rattan palm", "Spiny climbing palm with hooked tendrils that snag skin and clothes. Back out gently."},
  {15, "Native stinging nettle", "stinging nettle|nettle|nettles|giant nettle", "Hairy leaves in disturbed forest. The sting burns for hours. Lift hairs with tape."},
  {15, "Cycads", "cycad|macrozamia|burrawang|zamia|zamia palm", "Red seeds are toxic to people and animals. Never eat any part. Poisons Information: 13 11 26."},
  {15, "Castor oil plant", "castor oil|castor bean|castor oil plants", "Weed with spiky seed pods. The seeds are highly toxic if eaten. Call 13 11 26 if swallowed."},
  {15, "Wild berries and mushrooms", "mushroom|mushrooms|fungi|fungus|toadstool|toadstools|berries|wild berries|wild mushrooms", "Many are toxic. Never eat unknown plants or mushrooms. Poisons info 13 11 26."},
  // ---- Rainforest trees
  {16, "Strangler fig", "strangler figs", "Starts as a seed high in a host tree, drops roots, then envelops it. Fruit feeds wildlife."},
  {16, "Moreton Bay fig", "moreton bay figs|ficus macrophylla|big buttress fig", "Huge spreading tree with buttress roots and purple-red figs. A magnet for birds and bats."},
  {16, "Bunya pine", "bunya|bunya nut|bunya pines|bunya nuts", "Tall conifer with huge spiky cones. Heavy cones fall in summer; don't linger beneath."},
  {16, "Hoop pine", "hoop pines|araucaria cunninghamii", "Native conifer with horizontal bark rings and a tufted crown. Common in SEQ dry rainforest."},
  {16, "Antarctic beech", "antarctic beeches|nothofagus", "Ancient cool-rainforest tree of high ranges. Gnarled, mossy trunk; new leaves orange-red."},
  {16, "Red cedar", "red cedars|toona ciliata", "Deciduous rainforest tree that was heavily logged. It loses its leaves in winter."},
  {16, "Black bean", "moreton bay chestnut|black beans|castanospermum", "Big pods hold toxic seeds: never eat them. Orange-red flowers attract birds and bats."},
  {16, "Bangalow palm", "bangalow palms|piccabeen|piccabeen palm|archontophoenix", "Tall slender palm of creeks and gullies with a green crownshaft and bright red fruit."},
  {16, "Cabbage tree palm", "cabbage palm|cabbage tree|cabbage palms|livistona australis", "Tall fan palm of wet forest and creek flats. Keep clear of heavy fallen fronds."},
  {16, "Brush box", "brush boxes", "Tall tree with thick rough bark on the lower trunk and smooth bark above. Common in wet forest."},
  {16, "Blueberry ash", "blueberry ashes|elaeocarpus", "Small tree with white fringed flowers and bright blue berries, common at rainforest edges."},
  {16, "Lilly pilly", "lilly pillies|syzygium|pilly", "Glossy-leaved rainforest trees with fleshy pink or purple berries that birds and bats love."},
  {16, "Wild macadamia", "macadamia|macadamias|macadamia nut|macadamia nuts", "Native to SEQ rainforest. The nuts are very hard; leave wild nuts for wildlife."},
  {16, "Yellow carabeen", "carabeen|yellow carabeens", "Tall rainforest tree with buttressed roots and creamy bark. Common in cool, wet gullies."},
  {16, "Illawarra flame tree", "flame tree|flame trees|brachychiton", "Bare branches covered in red bell flowers in late spring and summer."},
  // ---- Eucalypts and forest trees
  {17, "Blackbutt", "blackbutts|blackbutt gum", "Tall gum with rough dark bark on the lower trunk, smooth pale above. Common coastal forest."},
  {17, "Spotted gum", "spotted gums|corymbia maculata|lemon scented gum", "Smooth bark that sheds in patches for a mottled look. Winter flowers feed wildlife."},
  {17, "Ironbark", "ironbarks|grey ironbark|narrow leaved ironbark|iron bark", "Deeply furrowed, dark bark that looks fire-blackened. Flowers feed nectar-loving birds and bats."},
  {17, "Scribbly gum", "scribbly gums", "Smooth pale bark with scribble-like tracks made by moth larvae tunnelling under it."},
  {17, "Bloodwood", "bloodwoods|red bloodwood", "Rough, tessellated bark with red sap that oozes like blood. Common in dry forest and heath."},
  {17, "Forest red gum", "forest red gums|queensland blue gum|blue gum|eucalyptus tereticornis", "Smooth, mottled bark. An important koala food tree and a rich source of nectar."},
  {17, "Swamp mahogany", "swamp mahoganies|eucalyptus robusta", "Rough, stringy bark and dense glossy crown. Grows on coastal wetlands and is a key koala tree."},
  {17, "Tallowwood", "tallowwoods|eucalyptus microcorys", "Soft, fibrous, yellow-brown bark. Big forest tree whose hollows and flowers support wildlife."},
  {17, "Moreton Bay ash", "moreton bay ashes|corymbia tessellaris", "Black, rough bark on the lower trunk and smooth white above. Common SEQ forest tree."},
  {17, "River oak", "river oaks|river she oak|casuarina cunninghamiana", "Slender, drooping foliage along creek banks that holds the soil. The wind makes a soft whisper."},
  {17, "Forest oak", "forest oaks|forest she oak|allocasuarina", "Drooping foliage and woody cones. Glossy black-cockatoos feed on its seeds."},
  // ---- Coastal and heath plants
  {18, "Coast banksia", "=banksia|=banksias|saw banksia|wallum banksia|banksia integrifolia", "Gnarled tree with chunky yellow flower spikes. Honeyeaters and lorikeets swarm to the nectar."},
  {18, "Grass tree", "grasstree|grass trees|xanthorrhoea", "Slow-growing, with a skirt of grass-like leaves. The tall flower spike rises after fire."},
  {18, "Pandanus", "screw pine|pandanus tectorius|pandanus palm|pandanus trees", "Stilt roots and spiral, serrated leaves. Fruit looks like a big pineapple. Common by beaches."},
  {18, "Paperbark", "=melaleuca|paperbarks|broad leaved paperbark", "Papery, layered bark and bottlebrush-like flowers. Grows in swamps and wet heath."},
  {18, "Coastal she-oak", "beach she oak|casuarina equisetifolia|beach casuarina", "Feathery foliage and rounded cones. Helps stabilise dunes and rustles in sea breezes."},
  {18, "Pigface", "pigface plant|carpobrotus|beach pigface", "Succulent beach groundcover with large pink or purple flowers. Helps hold sand dunes."},
  {18, "Spinifex", "beach spinifex|spinifex grass|spinifex hirsutus", "Silvery, rolling grass on frontal dunes that traps sand. Stay on boardwalks and don't trample it."},
  {18, "Goat's foot", "goats foot|beach morning glory|ipomoea pes caprae|goat foot", "Beach vine with pink flowers and notched leaves. Helps hold dunes together."},
  {18, "Grey mangrove", "grey mangroves|avicennia|mangrove|mangroves|white mangrove", "Tidal tree with pencil-like roots poking from the mud. Leaves excrete salt."},
  {18, "River mangrove", "river mangroves|aegiceras", "Small tree with curved, horn-like fruit. Grows along creeks in the upper tidal zone."},
  {18, "Red mangrove", "red mangroves|rhizophora|stilt root mangrove", "Tall arching prop roots hold it in soft mud. Long pencil-like seedlings hang from branches."},
  {18, "Coastal hibiscus", "=cottonwood|beach hibiscus|hibiscus tiliaceus|=cotton tree", "Yellow flowers that turn orange-red by evening. Thicket-forming tree at the high-tide line."},
  {18, "Bottlebrush", "bottlebrushes|callistemon", "Red flower spikes like a brush, loved by honeyeaters and lorikeets. Grows by creeks and swamps."},
  {18, "Wallum heath", "wallum|wallum heathland|coastal heath", "Sandy coastal heath with banksias, grass trees and wildflowers. Flowers late winter and spring."},
  // ---- Wildflowers
  {19, "Wattle", "acacia|acacias", "Golden pom-pom flowers, mainly in winter and spring. The seed pods feed parrots and cockatoos."},
  {19, "Flannel flower", "flannel flowers|actinotus|actinotus helianthi", "Soft, felt-textured white daisy with green-tipped petals. Blooms spring and summer."},
  {19, "Christmas bells", "christmas bell|blandfordia", "Red and yellow bells on tall stalks in damp coastal heath in early summer. Never pick them."},
  {19, "Purple coral pea", "hardenbergia|false sarsaparilla", "Scrambling vine with purple pea flowers from late winter. A spring treat along sandy tracks."},
  {19, "Native violet", "native violets|viola hederacea", "Creeping groundcover with small violet and white flowers in damp, shady places."},
  {19, "Sundew", "sundews|drosera|carnivorous plant|carnivorous plants", "Tiny carnivorous plant with sticky red tentacles that trap insects. In wet heath and sandy soils."},
  {19, "Greenhood orchid", "greenhood orchids|pterostylis|greenhood", "Small, hooded flowers on thin stems in forest in winter and spring. Watch your step."},
  {19, "Donkey orchid", "donkey orchids|diuris", "Yellow and brown flowers with donkey-ear petals appear from late winter in grassy forest."},
  {19, "Rock orchid", "rock orchids|sydney rock orchid|dendrobium speciosum|king orchid", "Large creamy flower spikes on rocks and trees in spring. Native orchids are protected."},
  {19, "Native ginger", "blue ginger|alpinia|alpinia caerulea", "Broad, shiny leaves, white flowers, and bright blue berries in damp rainforest understorey."},
  {19, "Native hibiscus", "native rosella|hibiscus heterophyllus|native hibiscus rose", "Large pink and white flowers on a tall prickly shrub in rainforest gullies, spring to summer."},
  {19, "Everlasting daisy", "paper daisy|paper daisies|everlasting daisies|everlastings|xerochrysum", "Papery yellow flowers that hold their shape when dry. Blooms in open forest and heath."},
  // ---- Ferns and palms
  {20, "Tree fern", "tree ferns|cyathea|rough tree fern|soft tree fern|man fern", "Slow-growing, with a fibrous trunk and a crown of arching fronds. Common in wet gullies."},
  {20, "Prickly tree fern", "prickly treefern|leichhardt tree fern", "Spiny trunk and stalks in rainforest and wet gullies. Mind your hands."},
  {20, "King fern", "king ferns|angiopteris", "Huge, glossy fronds metres long in sheltered rainforest gullies."},
  {20, "Bird's nest fern", "birds nest fern|birdnest fern|asplenium nidus|birds nest", "Glossy apple-green fronds forming a nest in tree forks. An epiphyte; harmless to the tree."},
  {20, "Staghorn and elkhorn", "staghorn|staghorns|staghorn fern|elkhorn|elkhorn fern|platycerium", "Antler-like fronds on tree trunks. Epiphytes; leave them in place."},
  {20, "Basket fern", "basket ferns|drynaria", "Epiphytic fern with brown basket leaves that catch leaf litter. On tree trunks and rocks."},
  {20, "Bracken", "bracken fern|pteridium", "Tall, coarse fern that spreads on open or disturbed ground and re-sprouts after fire."},
  {20, "Maidenhair fern", "maidenhair|adiantum|maidenhair ferns", "Delicate, fan-shaped fronds on dark, wiry stems near wet rocks and creeks."},
  {20, "Walking stick palm", "walkingstick palm|linospadix|walking stick palms", "Slender understorey palm with bright red fruit in rainforest."},
  // ---- Weeds
  {21, "Lantana", "lantana camara|lantana bush", "Spiky shrub with multicoloured flowers. Invasive; berries toxic. Clean seeds off your boots."},
  {21, "Camphor laurel", "camphor|camphor trees|camphor laurels|cinnamomum camphora", "Invasive tree that smells of camphor when crushed. Spread by birds; outcompetes natives."},
  {21, "Cat's claw creeper", "cats claw creeper|cats claw|dolichandra", "Weed vine with yellow trumpet flowers that smothers trees. Its hooks cling to bark."},
  {21, "Madeira vine", "anredera|mignonette vine|=madeira", "Invasive climber with heart-shaped leaves and small aerial tubers that spread easily."},
  {21, "Privet", "chinese privet|small leaved privet", "Invasive tree that shades out creek banks. Spread by birds; white flower clusters in spring."},
  {21, "Singapore daisy", "wedelia|sphagneticola", "Creeping weed with yellow daisies that smothers native groundcovers along creeks and tracks."},
  {21, "Giant rat's tail grass", "giant rats tail grass|rats tail grass|sporobolus", "Tall tussock weed; seeds stick to boots and vehicles. Avoid infested paddocks."},
  {21, "Mother-of-millions", "mother of millions|bryophyllum|mother of millions plant", "Succulent that drops plantlets everywhere. Poisonous to animals. Don't dump garden waste."},
  {21, "Bitou bush", "bitou|bitou bushes", "Invasive coastal weed with yellow daisies that smothers dunes. Report new patches."},
  {21, "Weed hygiene", "clean boots|weed seeds|boot hygiene|spreading weeds|do not spread weeds", "Brush off boots, socks and packs before and after a walk. Stay on the track."},
  // ---- Wildlife calendar
  {22, "Spring wildlife", "spring flowers|wildlife in spring|spring birds|spring animals", "Wildflowers bloom, magpies swoop, lapwings nest, snakes wake, shorebirds arrive."},
  {22, "Summer wildlife", "wildlife in summer|summer birds|summer animals", "Hot and humid. Cicadas, koels, frogs after rain, more ticks, turtle nesting. Carry water."},
  {22, "Autumn wildlife", "wildlife in autumn|autumn birds|autumn animals", "Milder, drier, and good walking weather. Banksias flower and snakes slow as it cools."},
  {22, "Winter wildlife", "wildlife in winter|winter birds|winter animals", "Cool, dry. Humpback whales pass the coast, wattles flower, and snakes are sluggish."},
  {22, "Snake season", "when are snakes active|snake activity|snake months", "Most active about Sep to Mar, but warm winter days bring them out. Wear boots; watch your step."},
  {22, "Swooping season", "swooping|swooping birds|magpie swooping|magpies swooping|swoop", "Roughly Aug to Oct. Walk away calmly, protect your head, wear a hat. Don't wave or run."},
  {22, "Whale season", "whale migration|when are whales|whale watching", "Humpbacks pass the SEQ coast roughly June to November, going north then back south with calves."},
  {22, "Turtle nesting season", "turtle nesting|turtle hatchlings|turtle season", "Turtles nest on beaches about Nov to Mar. Keep lights off, avoid nests, dogs away."},
  {22, "Shorebird season", "shorebird migration|migratory shorebirds|migratory birds|shorebird arrivals", "Migratory shorebirds reach Moreton Bay about Aug and leave about Mar. Keep clear of roosts."},
  {22, "Wildflower season", "best time for wildflowers|when do flowers bloom|when do wildflowers bloom", "Heath wildflowers peak in late winter and spring, wattles in winter, orchids from Aug."},
  // ---- Tracks and signs
  {23, "Koala signs", "koala scats|koala scratches|koala poo|koala scat", "Oval grey-brown pellets under gums and claw scratches on smooth trunks mean koalas are near."},
  {23, "Wallaby and kangaroo signs", "wallaby scats|kangaroo scats|kangaroo tracks|wallaby tracks|roo poo", "Round pellets in grassy clearings and long narrow hind-foot prints."},
  {23, "Echidna signs", "echidna diggings|echidna scat|echidna tracks|echidna holes", "Small dug cone-shaped pits and torn-open logs or ant nests. The scat is a crumbly soil tube."},
  {23, "Bandicoot diggings", "bandicoot holes|bandicoot signs|conical holes", "Lots of small conical holes in lawn and leaf litter where they dug for grubs and worms."},
  {23, "Brush-turkey mound", "brush turkey mound|turkey mound|scrub turkey mound|bird mound", "A big pile of leaf litter and soil. Rotting leaves incubate the eggs. Leave it alone."},
  {23, "Snake tracks", "snake trail|snake track", "A wavy line in sand or dust with gentle S-curves. Give it a wide berth and watch your step."},
  {23, "Glider feeding notches", "glider notches|glider signs|glider scars", "V-shaped scars on gum trunks mean gliders feed here on sap."},
  // ---- Wildlife care
  {24, "Injured wildlife", "injured animal|hurt animal|sick animal|found injured animal|orphaned wildlife|joey|joeys", "Call RSPCA 1300 ANIMAL (1300 264 625). Stay back, quiet and calm. Don't feed or give water.~Marine strandings: 1300 130 372 (option 1). Sick or dead birds: 1800 675 888. Never touch bats."},
  {24, "Don't feed wildlife", "do not feed wildlife|feeding wildlife|feeding birds|feed the birds|feeding animals", "Human food makes wildlife sick and aggressive. Please don't feed them; take rubbish home."},
  {24, "Dogs and wildlife", "dogs and koalas|dog koala|dogs wildlife|dogs near wildlife", "Most Qld national parks ban dogs. Elsewhere, keep dogs on a lead and away from wildlife."},
  {24, "Watching wildlife", "wildlife etiquette|wildlife photography|photographing wildlife|wildlife viewing", "Keep your distance, move slowly, no flash at night, avoid nests and dens, never chase."},
  {24, "Koala on the ground", "koala on ground|koala on the track|koala on the road|koala crossing", "Keep dogs away and give it space. If it looks injured or is on a road, call 1300 264 625."},
  {24, "Wildlife hotline", "report wildlife|wildlife incident|parks wildlife hotline|qld parks wildlife hotline", "Qld parks wildlife hotline 1300 130 372: incidents and marine strandings (option 1)."},
};
#define WILD_COUNT ((int)(sizeof(WILDLIFE) / sizeof(WILDLIFE[0])))
