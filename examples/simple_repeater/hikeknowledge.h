#pragma once
/*
 * Hiking knowledge for the bot (HikeBot.h). Compiled into flash. To change what the bot says, edit this file and
 * rebuild; tools/hikebot_test checks the format and every message length.
 *
 * A topic is: { keys, priority, text }
 *   keys      keyword phrases separated by ';' that match whole words anywhere in the message. The message is
 *             lowercased, punctuation and apostrophes removed and contractions expanded first ("what's" becomes
 *             "what is", "can't" becomes "cannot"), so keys use lowercase letters, digits and single spaces only.
 *   priority  the highest matching topic wins. 60 and up beats a plain trail name in the message.
 *   text      variants separated by '|' (used in turn, so the bot does not repeat itself). Each variant is up to
 *             4 messages separated by '~'. Keep every message to HIKE_PART_MAX (110) characters or fewer, so it
 *             fits a repeater name of up to 22 characters; with a longer name the bot splits a message at a sentence break. Never start a message with '!' and never use '~' or '|'.
 *
 * Safety rules in chatrules.h (emergencies, lost, snakes, storms, low water ...) are checked first and always win.
 * Emergency numbers were checked against Queensland Government and QFES pages; re-check them now and then.
 */

#include <stdint.h>

#define HIKE_PART_MAX 110

struct HikeTopic {
  const char* keys;
  uint8_t prio;
  const char* text;
};

static const HikeTopic HIKE_TOPICS[] = {
  // ---- what the bot can do ----
  {"what can you do;what do you do;how do you work;how does this work;how to use you;what can i ask;what can i ask you;"
   "instructions;commands;menu;help;how do i use you;what are you for", 30,
   "I'm a hiking helper. Ask about trails, what to take, how long, when to start, sunset, the moon or meteors.~"
   "Say I am starting now, then finished hike, to time a walk. Ask for a walk idea, or say radio check.~"
   "Type !trails to browse by region. I can't see weather or closures: check parks.qld.gov.au."},

  // ---- numbers and getting help ----
  {"emergency number;emergency numbers;triple zero;who do i call;who to call;phone number;phone numbers;lifeline;"
   "poisons;poison;emergency app;emergency plus;112;000;ses;13health;parks phone;ranger;rangers;park rangers;parks contact", 70,
   "Emergency: call 000 (112 from a mobile, 106 for TTY). The Emergency+ app shows your GPS position.~"
   "Lifeline 13 11 14. Poisons Info 13 11 26. SES (storm, flood) 132 500. 13HEALTH 13 43 25 84.~"
   "Parks and general info: 13 QGOV (13 74 68), business hours. With no signal, use your PLB."},

  {"plb;epirb;locator beacon;personal locator beacon;rescue beacon;distress beacon;emergency beacon", 80,
   "A PLB is a 406 MHz beacon that sends your position to rescuers by satellite, with no phone signal.~"
   "Carry it on you, not buried in the pack. Register it with AMSA, and use it only in a real emergency.~"
   "An EPIRB is the boat version; on foot a PLB is the better pick. Mesh and phones are backups."},

  {"no signal;no reception;no coverage;no service;no phone signal;no mobile signal;out of range;no bars;cannot call;"
   "cannot get through;how do i get help;how do i call for help;get help", 70,
   "No signal? Stay put unless it's unsafe, and activate your PLB: it works without phone coverage.~"
   "A phone can often reach 000 on any network, even with no bars. Try from high, open ground.~"
   "Signal with 3 whistle blasts, a mirror or bright clothing. Stay visible, warm and hydrated."},

  {"is mesh enough;mesh enough;replace plb;mesh instead of plb;mesh or plb;rely on mesh;rely on meshcore;mesh reliable;"
   "meshcore reliable;can i rely on", 85,
   "Mesh coverage is patchy, especially in valleys. It can help, but it isn't guaranteed to reach anyone.~"
   "Carry a PLB as your reliable way to call for help. Treat mesh and phone as extras."},

  {"trip plan;tell someone;leave a plan;trip intentions;intentions form;check in;check in time;overdue;"
   "who should i tell", 65,
   "Before you go, tell someone your route, start time and when you'll be back, and what to do if you're not.~"
   "I can't raise an alarm if you're overdue, so agree a time and who calls 000 if you miss it."},

  // ---- first aid and hazards ----
  {"first aid;first aid kit;medical kit;bandage;bandages;plasters", 65,
   "First aid kit: 2 wide elastic bandages (snake bite), dressings, triangular bandage, tape, gloves.~"
   "Add antiseptic, blister plasters, pain relief, scissors, a space blanket and your own medicines.~"
   "A first aid course teaches far more than a kit does. St John and Red Cross run them."},

  {"snake bite kit;snakebite kit;pressure bandage;pressure bandages;pressure immobilisation;compression bandage;"
   "snake bandage", 66,
   "For snake bite carry 2 wide elastic bandages (15 cm). Wrap firmly over the bite and up the whole limb.~"
   "Splint the limb, keep the person still, and call 000 or use the PLB. Don't cut, suck or use a tourniquet.~"
   "Don't wash the bite. Practise the technique on a first aid course before you need it."},

  {"stinging tree;stinging trees;gympie gympie;gympie;stinger tree;gympie stinger", 60,
   "Stinging trees have big, hairy, heart-shaped leaves. A touch causes severe pain that can last weeks.~"
   "Don't rub. Lift the hairs with wax hair-removal strips or sticky tape, then get medical help.~"
   "In rainforest, especially off-track, don't brush unknown leaves. Gloves and long sleeves help."},

  {"heat stroke;heatstroke;heat exhaustion;heat illness;sunburn;sunstroke;overheating", 60,
   "Heat illness: move to shade, loosen clothing, cool the skin with water, and sip fluids slowly.~"
   "Call 000 if they're confused, stop sweating or collapse, and keep cooling until help arrives.~"
   "Prevent it: start early, rest in shade, drink often, wear a hat and cover up."},

  {"hypothermia;shivering;too cold;exposure", 60,
   "Hypothermia: get out of wind and rain, remove wet clothes, add dry layers, insulate from the ground.~"
   "Give warm sweet drinks and food if they're alert. Call 000 or use your PLB if they get drowsy."},

  {"blister;blisters;hot spot;hot spots", 60,
   "Stop at the first hot spot. Cover it with a blister plaster or tape, and don't burst blisters.~"
   "Keep feet dry and break your boots in before a long walk."},

  {"sprain;sprained;twisted ankle;rolled ankle;sprained ankle;injury;injured", 60,
   "Sprain: rest, strap the joint firmly with a bandage and keep it raised. Walk out only if it's safe.~"
   "If you can't bear weight or you're far from help, stay put and use your PLB or call 000."},

  {"cliff;cliffs;edge;edges;exposed;selfie;selfies", 60,
   "Stay on the track and behind barriers near cliffs and gorges. Edges crumble and wet rock is slick.~"
   "Never take selfies near an edge, and keep children close. Many serious falls happen at lookouts."},

  {"allergic;allergy;anaphylaxis;epipen;wasp;wasps;bee;bees", 60,
   "Allergic to stings? Carry your adrenaline auto-injector and tell your group where it is.~"
   "For severe swelling, trouble breathing or faintness, call 000 straight away."},

  {"drink creek water;creek water;purify;purification;water tablets;filter water;treat water;safe to drink", 60,
   "Treat creek water before drinking: boil it for a minute, or use a filter or purification tablets.~"
   "Don't count on finding water. Carry what you need for the whole walk, plus a spare litre."},

  {"lighter;matches;campfire;fire ban;fire bans;fire danger;bushfire;bushfires;light a fire", 60,
   "Carry a lighter or waterproof matches in a dry bag for emergencies. Check for fire bans first.~"
   "Never light a fire in a fire ban or where fires aren't allowed. Check qfes.qld.gov.au and park alerts."},

  // ---- walking facts ----
  {"grade scale;grading;what does grade;grade 1;grade 2;grade 3;grade 4;grade 5;grades;what is grade;"
   "what do grades mean;difficulty grades;gr 1;gr 2;gr 3;gr 4;gr 5", 70,
   "Qld Parks grades: 1 is flat and easy. 2 is gentle with a few steps. 3 has hills, steps and uneven ground.~"
   "4 is steep and rough, sometimes unmarked: fitness and bushwalking experience needed. 5 is experts only."},

  {"how long would it take;how long should i allow;how long does a hike take;walking speed;walking pace;"
   "how fast do people walk;how long for a hike;estimate time;naismith;pace", 70,
   "Rule of thumb: about 4 km/h on easy track, 2 to 3 km/h when steep or rough, 1 to 2 off-track. Add rests.~"
   "Add time for climbs, kids, photos and breaks. Time a walk with me: say I am starting now."},

  {"how much water;how many litres;how many liters;litres of water;water per hour;how much to drink;"
   "how much should i drink;water should i take;water do i need;water bottle;hydration", 80,
   "A good baseline is 1 L per hour of walking in warm weather, or about 0.5 L if it's cool and easy.~"
   "Carry more for steep, long or hot days. Add electrolytes, and turn back early if you run low."},

  {"best time of year;best season;best time to go;summer;winter;autumn;spring;wet season;dry season;"
   "when is the best time;seasons;season;humid;mist;fog", 60,
   "SE Qld: summer (Dec to Feb) is hot, humid and stormy. Start early and finish before afternoon storms.~"
   "Autumn to spring is drier and milder, but plateaus like Springbrook and Lamington can be cold and misty.~"
   "Falls and creeks flow best after rain and are low when dry. Spring brings higher bushfire risk."},

  // ---- off-trail ----
  {"off track;off trail;offtrack;off tracking;bushbash;bush bash;bush bashing;bushbashing;cross country;"
   "off the beaten;pathless;scrambling", 65,
   "Off-track walking is slow: allow roughly double the time, and turn back early to beat the dark.~"
   "Tell someone your route and return time, and carry a PLB. Mesh and phones fail in gullies and dense bush.~"
   "Carry a map, compass and offline map. Use ridges and creeks as handrails and note landmarks.~"
   "Cover up against scrub, bring extra water, a head torch and warm layer. Check closures at parks.qld.gov.au."},

  // ---- glow worms (used when the clock is not set; HikeBot.h handles the timing otherwise) ----
  {"glow worm;glow worms;glowworm;glowworms", 65,
   "Glow worms need full darkness: go 30 to 45 minutes after sunset. Natural Bridge in Springbrook is well known.~"
   "Take a head torch for the track, then use a dim or red light. No flash, and don't touch the worms or walls."},

  // ---- MeshCore ----
  {"better range;more range;improve range;improve signal;get signal;get better signal;boost signal;how far does lora;"
   "how far does mesh;how far can;mesh range;lora range;antenna tips;meshcore tips;mesh tips;range tips", 70,
   "Height and line of sight matter most. Ridges, lookouts and clearings beat gullies and thick forest.~"
   "Keep the antenna upright and clear of your body and pack, and never transmit without it attached.~"
   "Hills, wet bush and your body all block LoRa. Moving a few metres up, or to a gap, can fix it."},

  {"why cannot you hear me;not hearing me;no reply;no response;not getting through;why no reply;cannot reach;"
   "why is my signal weak;weak signal;poor signal;low signal;bad signal", 70,
   "Likely a hill, gully or dense bush in the way. Climb to higher, open ground and try again.~"
   "I may be asleep at times, so no reply doesn't always mean no coverage. Try again in a few minutes."},

  {"what is snr;what does snr mean;what is rssi;what does rssi mean;what are hops;what does hops mean;snr;rssi;hops;"
   "read the numbers;what do the numbers mean;understand test", 70,
   "SNR is signal against noise: above 0 dB is solid, and around -7 dB is the edge of what LoRa can decode.~"
   "RSSI is signal power: about -90 dBm is strong, -110 is weak, below -120 is very weak.~"
   "Hops are the repeaters your message passed through. 0 means I heard you directly."},

  {"battery life;power bank;charge;batteries;flat battery;cold battery", 60,
   "Carry a power bank, and keep spare batteries warm in an inner pocket: cold drains them fast.~"
   "With no phone signal, airplane mode saves a lot of battery. Dim the screen, too."},
};
#define HIKE_TOPIC_COUNT ((int)(sizeof(HIKE_TOPICS) / sizeof(HIKE_TOPICS[0])))

// ---------------------------------------------------------------------------------------------------------------
// gear: the same three messages for every walk, then one with extras for the walk or the conditions
static const char HIKE_GEAR_BASE[] =
  "Comms: PLB or EPIRB, phone and power bank, whistle, MeshCore radio (patchy in valleys).~"
  "First aid kit with 2 wide pressure bandages (snake bite), blister care, personal meds.~"
  "Water (1 L an hour), food, head torch, lighter or matches, map, hat, sunscreen, rain jacket.";
