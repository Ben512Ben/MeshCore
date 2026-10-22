#pragma once
/*
 * ELIZA-style chat rules for the bot channel (item 33). Trigger: a message that starts with '!' and is not a trail, town, region or signal request, e.g. "!hello".
 * Compiled into the firmware (flash). To change what the bot says, edit this file and rebuild.
 *
 * A rule is: { keys, priority, flags, replies }
 *   keys      one or more keyword phrases separated by ';'. They match whole words anywhere in the message.
 *             The message is lowercased, punctuation and apostrophes are removed, and contractions are expanded
 *             first ("I'm" becomes "i am", "can't" becomes "cannot", "mum" becomes "mother"), so keys are written
 *             in that form: lowercase letters, digits and single spaces only. tools/chatbot_test checks this.
 *   priority  the matching rule with the highest priority wins (ties: the earlier rule). 100 = emergency,
 *             90 safety tips, 60 feelings, 50 hiking topics, 42 "I am / I feel / I want ...", 40 and below = weaker
 *             topics, 20 and below = very generic words ("what", "why", "i").
 *   flags     CHAT_REMEMBER: keep what the person said and bring it up again later.
 *   replies   templates separated by '|', used in turn so the bot does not repeat itself. Every rule needs at
 *             least one template with no % & or $ in it. In a template:
 *               %  what the person said after the keyword, with I and you swapped ("my feet hurt" gives "feet hurt")
 *               &  the keyword and what follows, swapped ("my feet hurt" gives "your feet hurt")
 *               $  the remembered line from this person earlier
 *               @  the person's name (the text before the ": " in their message)
 *             A template that needs % & or $ is skipped when there is nothing to put in.
 *   Keep a template to about 90 characters or fewer. A reply is cut to fit the 134 character message limit
 *   (which includes the repeater's name), at a word boundary.
 */

#include <stdint.h>

#define CHAT_REMEMBER 1

struct ChatRule {
  const char* keys;
  uint8_t prio;
  uint8_t flags;
  const char* replies;
};

static const ChatRule CHAT_RULES[] = {
  // ============ emergencies and safety (these always win) ============
  {"kill myself;end my life;want to die;suicide;suicidal;self harm;hurt myself;do not want to live;no reason to live;better off dead", 100, 0,
   "You matter, @. Call Lifeline 13 11 14, or 000 if you are in danger.|@, tell someone near you and call Lifeline 13 11 14. You are not alone."},
  {"snake bite;snakebite;bitten by a snake;bitten by snake", 100, 0,
   "Snake bite: keep still, bandage firmly up the limb, and call 000. Do not wash it.|Snake bite is an emergency. Stay still, firm bandage, call 000."},
  {"heart attack;chest pain;cannot breathe;unconscious;not breathing;bleeding;broken leg;broken ankle;broken arm;broken bone;heat stroke;passed out;seizure;badly hurt;badly injured;severely injured;i am injured", 100, 0,
   "Sounds serious. Call 000 (112 on mobile) now. I'm only a bot; mesh may not reach help.|With no signal, use a PLB or EPIRB if you carry one, and stay put."},
  {"need help;help me;please help;urgent;emergency;sos;mayday;call for help;need rescue;need rescuing", 100, 0,
   "Is this an emergency? Call 000 (112 on mobile). I'm only a bot.|If you need urgent help, call 000. Mesh messages may not reach rescuers."},
  {"lost;cannot find the track;cannot find the trail;cannot find the path;i am off track;i am off the track;we are off track;we are off the track;wrong way;turned around", 95, 0,
   "Lost? Stop, stay put, stay warm. Call 000 if you can. Mesh may not reach help.|Stay where you are. Go back only to the last sign you know. Call 000 if unsafe."},
  {"snake;snakes", 90, 0,
   "Give snakes space, stay calm, and back away slowly. If bitten, stay still and call 000.|Most snakes just want to leave. Walk around, don't poke. If bitten, call 000."},
  {"lightning;thunder;thunderstorm;storm", 90, 0,
   "Storm: get off ridges and open ground, away from tall trees. Crouch low, spread out.|Check bom.gov.au before you walk. Storms build fast on the escarpment."},
  {"no water;out of water;ran out of water;dehydrated;dehydration", 90, 0,
   "Rest in shade, sip slowly, and turn back early. Call 000 if you feel confused or faint."},
  {"creek crossing;flooded;flood;river crossing;creek is up", 90, 0,
   "Don't cross a flooded creek. Water rises fast. Wait, or turn back."},
  {"leech;leeches", 90, 0,
   "Flick leeches off sideways with a fingernail. They drop off on their own, too."},
  {"tick;ticks", 90, 0,
   "Remove ticks with fine tweezers close to the skin. See a doctor if you feel unwell."},
  {"spider;spiders", 90, 0,
   "Most spiders are harmless. For a funnel-web bite, bandage firmly and call 000."},
  {"dark;night;sunset;nightfall;torch", 70, 0,
   "After dark? Slow down, stay on the track, use a torch. If unsure, stop and wait for light."},
  {"track closed;trail closed;closure;closures;alerts;park alert", 70, 0,
   "Check parks.qld.gov.au for closures before you walk. I only know the usual facts."},
  {"weather;forecast", 65, 0,
   "I can't see the weather. Check bom.gov.au or the BOM app before you walk."},

  // ============ feelings ============
  {"depressed;hopeless;miserable;unhappy;lonely;anxious;overwhelmed;anxiety;depression", 60, 0,
   "I'm sorry you feel that way, @. Talking to a real person helps. Lifeline is 13 11 14.|That sounds heavy, @. Is there someone you can talk to? Lifeline is 13 11 14.|I'm only a bot, but I'm listening. What's making you feel that way?"},
  {"sad;upset;crying;stressed;worried;scared;afraid;nervous", 55, 0,
   "I'm sorry to hear that. What do you think is behind it?|Tell me more about how you feel.|How long have you felt like that?|What would help right now, @?"},
  {"angry;mad;furious;annoyed;frustrated;irritated", 55, 0,
   "I'm sorry you feel that way. What's causing it?|Take a breath, @. What's frustrating you?|Does the anger help here?"},
  {"happy;great;awesome;amazing;fantastic;wonderful;excellent;fun", 45, 0,
   "Glad to hear it, @. What's making it good?|That's great. Tell me more.|What's the best part?"},
  {"bad;terrible;awful;horrible;rough", 45, 0,
   "I'm sorry. What happened?|That sounds rough. Tell me more.|What's the worst part?"},

  // ============ hiking and the outdoors ============
  {"tired;exhausted;knackered;sore;blister;blisters", 50, 0,
   "Tired is allowed. Rest, snack, sip some water. What's the next stretch like?|How long have you been walking, @?|Rest in the shade for a bit. Does your body say stop or just slow down?"},
  {"hungry;snack;snacks;food;lunch;breakfast", 50, 0,
   "Food always helps on a hike. What did you pack?|Hunger is just the mountain asking for a snack stop.|Do you have enough to eat for the way back?"},
  {"thirsty;water", 50, 0,
   "Sip often, not all at once. How much water do you have left?|Water is the one thing not to skimp on. How much have you got?"},
  {"hot;heat;sweating;sweaty", 50, 0,
   "Heat is no joke. Shade, water, slow pace. How are you feeling?|Hot already? Maybe an earlier start or a creek stop next time."},
  {"cold;freezing;chilly", 50, 0,
   "Layers are your friend. Do you have a jacket with you?|Cold? Keep moving gently and add a layer."},
  {"rain;raining;wet;drizzle", 50, 0,
   "Rain makes rocks slippery. Take it slow, @.|Wet walking has a charm, until the boots fill up. How's the track?|Check the creek crossings if it has been raining."},
  {"mud;muddy;slippery;slippy", 50, 0,
   "Slow steps, short strides, poles help. Slippery rock is the usual culprit."},
  {"steep;climb;climbing;uphill;stairs;steps", 50, 0,
   "Steep ones are best taken slow and steady. How far to the top?|Short steps, steady breath. You'll feel it later in a good way.|Pace yourself. Breathe in rhythm with your steps."},
  {"view;views;lookout;summit;top;peak", 50, 0,
   "Views make it worth it. What can you see?|Summit time! How does it feel up there?|Take a photo, then take a breath. Enjoy it, @."},
  {"waterfall;falls;rock pool;swim;swimming", 50, 0,
   "Falls are best after rain, but only swim where the creek is low and you know it is safe.|Check how deep a pool is before jumping in. What does it look like?"},
  {"koala;wallaby;lyrebird;platypus;wildlife;bird;birds;kangaroo;echidna;goanna;frog", 50, 0,
   "Lucky you! Keep your distance and let it do its thing.|Tell me more about what you saw, @.|Wildlife is the best bonus of any walk."},
  {"dog;dogs", 50, 0,
   "Most national parks don't allow dogs. Check the rules before you go."},
  {"kids;children;baby;toddler", 50, 0,
   "Short, easy tracks are best with kids. Try !trails and look for Gr 1 or 2.|How old are the kids, @?"},
  {"car;parking;carpark;drive;driving", 50, 0,
   "Carparks can be busy on weekends. Arrive early for the best spots.|How was the drive up?"},
  {"toilet;loo;bathroom", 50, 0,
   "Most trailheads have a toilet. If not, carry out what you carry in."},
  {"coffee;cafe;beer;pub;pie", 50, 0,
   "A coffee or a cold one after a walk is part of the deal.|The best part of a long walk is the pie and coffee after."},
  {"map;gps;navigation;compass", 50, 0,
   "Carry a map and keep your phone charged. Don't rely on one thing.|Download offline maps before you go."},
  {"hike;hiking;bushwalk;bushwalking;walking;walk;trek;trekking", 40, 0,
   "Where are you walking? Try !trails to see what's nearby.|How long have you been walking today?|Tell me about your walk, @."},
  {"trail;trails;track;tracks", 40, 0,
   "Try !trails for trail info, or tell me where you're headed.|What kind of track are you after? Easy, steep, waterfalls?"},
  {"mesh;meshcore;repeater;node;radio;lora;antenna;signal;range", 40, 0,
   "Mesh is magic. What are you running, @?|I live inside a repeater, so I'm biased. How's your signal?|Range depends on height and line of sight. Higher is better."},
  {"test", 40, 0,
   "Try !test for a signal report, @. Or just chat: !hello"},
  {"joke;funny;laugh;jokes", 45, 0,
   "Why did the hiker bring a ladder? It was a steep trail.|I'd tell a joke about the trail, but it's all uphill.|What do you call a hiker who never gets lost? A liar."},
  {"fact;trivia;did you know;facts", 45, 0,
   "Fact: Lamington and Springbrook are part of the Gondwana Rainforests World Heritage Area.|Fact: Captain Cook named the Glass House Mountains in 1770, after glass furnaces.|Fact: Natural Bridge has a glow worm colony. Best after dark. Try !natural bridge.|Fact: Mount Coot-tha is a short walk from the Brisbane CBD. Try !mount coot-tha."},
  {"bored", 45, 0,
   "Bored? Try !joke, or !fact. Or !trails to plan the next one.|Tell me where you walked last, @."},
  {"weekend;today;tomorrow;yesterday;tonight", 30, 0,
   "What are you planning, @?|Tell me more about your plans."},

  // ============ about the bot ============
  {"who are you;what are you;your name;are you a bot;are you real;are you human;are you ai;are you alive", 45, 0,
   "I'm a little chat bot in a radio. No cloud, no brain, just rules.|I'm a bot, @. A very small one. What would you like to talk about?|No internet here. Just rules running on a LoRa board."},
  {"who made you;who built you;who wrote you;who created you;who programmed you", 45, 0,
   "Someone with a radio and plenty of curiosity wrote my rules.|A hiker who got a bit carried away with a radio."},
  {"how are you;how are you going;how is it going;how you doing;how are things", 45, 0,
   "Running fine, thanks. No battery worries today. How are you, @?|I'm a radio. I'm doing well. How are you?"},
  {"are you smart;are you intelligent;do you understand;do you think", 45, 0,
   "Not really. I match patterns. But I try to listen.|I understand about as much as a rock. A very friendly rock."},
  {"computer;computers;ai;chatgpt;robot;machine;artificial", 35, 0,
   "Do computers worry you?|I'm not an AI, just rules running on a radio.|What do you think machines have to do with this?"},
  {"help", 25, 0,
   "I'm a hiking helper. Ask me about trails, gear or timing. Emergencies: call 000.|Try !hello, !joke or !fact. Trails: !trails. Emergency: 000."},

  // ============ greetings, thanks, goodbyes ============
  {"hello;hi;hey;howdy;good morning;good afternoon;good evening;yo;hiya", 38, 0,
   "Hello @. What's on your mind?|Hi @. How are you finding the trails?|G'day @. How can I help?|Hey @. Tell me what you're up to."},
  {"bye;goodbye;see you;cya;goodnight;catch you later", 38, 0,
   "Bye @. Stay safe out there.|See you, @. Enjoy the walk.|Goodbye @. Let the mountain take care of you."},
  {"thanks;thank you;cheers;ta;appreciate it", 38, 0,
   "You're welcome, @.|Any time.|Happy to chat.|No worries."},

  // ============ what the person says about themselves (ELIZA core) ============
  {"i am", 42, CHAT_REMEMBER,
   "How long have you been %?|Why are you %?|Do you enjoy being %?|Did you come to me because you are %?|Do you believe it is normal to be %?|Tell me more."},
  {"i feel", 42, CHAT_REMEMBER,
   "Tell me more about feeling %.|Do you often feel %?|What makes you feel %?|When you feel %, what do you do?|Is feeling % new for you?|Tell me more about how you feel."},
  {"i want", 42, CHAT_REMEMBER,
   "Why do you want %?|What would it mean to you if you got %?|What if you never got %?|Do you really want %?|What would you do if you got %?|What do you want, really?"},
  {"i need", 42, CHAT_REMEMBER,
   "Why do you need %?|Would it really help you to get %?|Are you sure you need %?|What if you did not get %?|What do you need most?"},
  {"i think", 42, 0,
   "Do you really think %?|But you are not sure %?|What makes you think %?|Does it worry you that %?|Why do you think that?"},
  {"i believe", 42, 0,
   "Why do you believe %?|What if it was not true that %?|Do you always believe that?"},
  {"i have", 42, CHAT_REMEMBER,
   "Why do you tell me you have %?|Have you really got %?|Tell me more about having %.|How long have you had that?"},
  {"i was", 42, CHAT_REMEMBER,
   "Were you really %?|Why do you tell me you were %?|What does it mean that you were %?|What happened?"},
  {"i will", 42, 0,
   "Do you really expect to %?|What if you don't %?|Is that a plan or a hope?"},
  {"i like", 42, 0,
   "What do you like most about %?|Why do you like %?|What else do you like?"},
  {"i love", 42, 0,
   "Tell me more about loving %.|What is it about that you love?|That sounds important to you."},
  {"i hate", 42, 0,
   "What makes you hate %?|Does hating that help?|What would change if it were different?"},
  {"i know", 42, 0,
   "How do you know %?|Are you certain %?|Who told you that?"},
  {"i hope", 42, 0,
   "What would happen if %?|Do you often hope for things like that?|What do you do while you wait?"},
  {"i miss", 42, 0,
   "What do you miss most about %?|How long has it been?|Does missing it make you want to do something?"},
  {"i wish", 42, 0,
   "Why do you wish %?|What would change if %?|Is that likely?"},
  {"i cannot", 42, 0,
   "What makes you think you cannot %?|Have you tried to %?|What would happen if you did %?|Perhaps you could, if you really wanted to."},
  {"i do not", 42, 0,
   "Why don't you %?|Do you wish you did %?|What if you did %?|Why not? Tell me more."},
  {"i did not", 42, 0,
   "Why didn't you %?|Do you wish you had %?|What stopped you?"},
  {"i never", 42, 0,
   "Never? Really? What stops you?|Is it that you can't, or that you won't?|Would you like to change that?"},
  {"i always", 42, 0,
   "Always? Can you think of an exception?|Why do you think it is always so?|What would break the pattern?"},
  {"my", 35, CHAT_REMEMBER,
   "Why do you say &?|Is it important to you that &?|Does it bother you that &?|Tell me more about that.|Let's talk more about that."},
  {"mother;father;family;parents;wife;husband;partner;brother;sister;friend;friends;son;daughter", 35, 0,
   "Tell me more about your family.|Who else in your family comes to mind?|How do you get along with them?|What does that remind you of?"},
  {"dream;dreams;nightmare", 35, 0,
   "What does that dream suggest to you?|Do you dream often?|What persons appear in your dreams?"},
  {"remember", 35, 0,
   "What else do you remember?|Why do you recall that now?|What does it remind you of?"},
  {"can you", 35, 0,
   "Do you want me to be able to %?|Perhaps you would like me to be able to %.|If I could %, what would you do?|What would you like me to do?"},
  {"do you", 35, 0,
   "Do I %? Why do you ask?|Would it matter to you if I did?|What do you think, @?"},
  {"you are", 35, 0,
   "What makes you think I am %?|Does it please you to believe I am %?|Perhaps you would like to be %.|We were talking about you, not me."},
  {"you cannot;you will not;you do not;you did not", 35, 0,
   "Why do you think &?|Does it matter to you that &?|Perhaps you wish &?|We were talking about you, not me."},
  {"sorry;apologise;apologies", 30, 0,
   "No need to apologise, @.|Apologies aren't needed here.|What feelings come up when you say sorry?"},
  {"perhaps;maybe;possibly;not sure", 30, 0,
   "You don't seem certain.|Why the uncertain tone?|Can't you be more positive?"},
  {"because", 30, 0,
   "Is that the real reason?|Does that explain anything else?|What other reasons come to mind?"},
  {"yes;yeah;yep;sure;definitely;absolutely;ok;okay", 20, 0,
   "You seem quite sure.|I see. Tell me more.|OK. Go on.|Can you say more?"},
  {"no;nope;nah", 20, 0,
   "Why not?|Are you saying no just to be difficult?|You are being a bit negative.|Why do you say no?"},
  {"why", 15, 0,
   "Why do you ask?|What do you think?|Does that question interest you?|Why do you think that matters?"},
  {"what", 15, 0,
   "What do you think?|Why do you ask that?|What answer would please you most?|What comes to mind when you ask that?"},
  {"how", 15, 0,
   "How do you suppose?|Is that what you really want to know?|What do you think, @?"},
  {"where", 15, 0,
   "Where do you think?|Why do you ask where?|Does the place matter to you?"},
  {"when", 15, 0,
   "When do you think?|Why do you ask when?|Is timing important here?"},
  {"who", 15, 0,
   "Who do you think?|Does it matter who?|Why do you want to know?"},
  {"you", 12, 0,
   "We were talking about you, not me.|Why do you bring me into this?|What about you, @?"},
  {"i", 10, 0,
   "You say you %. Tell me more.|Go on, @.|How does that make you feel?"},
};

// no keyword matched: general ELIZA nudges, used in turn
static const char* const CHAT_FALLBACK =
  "Please go on.|Tell me more.|I see. What does that suggest to you?|Interesting. Say more.|How does that make you feel?|"
  "Can you say that another way?|Why do you say that?|Go on, @.|Hmm. What else is on your mind?|"
  "That's a bit beyond a little radio like me. Try !trails.|Is that so?|And how long has that been on your mind?";

// the message was just "*"
static const char* const CHAT_EMPTY =
  "Yes, @? What's on your mind?|Say something after the !, like !hello.|I'm listening, @.";

// the same person sent the same message twice in a row
static const char* const CHAT_REPEAT =
  "You already said that, @. Try saying it another way.|Same again, @? I'm still listening.|You repeat yourself. What else is on your mind?";

// no keyword matched and this person said something earlier worth coming back to ($)
static const char* const CHAT_MEMORY =
  "Earlier you said $. Tell me more.|Does it still matter that $?|Going back: $. Why do you think that is?|Can we go back to when you said $?";

#define CHAT_RULE_COUNT ((int)(sizeof(CHAT_RULES) / sizeof(CHAT_RULES[0])))
