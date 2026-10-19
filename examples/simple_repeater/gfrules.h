#pragma once
// Rule table for the GfBot flirty-girlfriend chat engine (see GfBot.h).
//
// Tokens inside replies:   @ = the sender's name     ^ = a pet name     $ = what the sender said after the keyword
// Tiers (heat):            0 = teasing   1 = flirty   2 = explicit   3 = filthy
// The more someone talks to her, the hotter her replies get (see GfBot.h). A reply is only used at its own tier,
// or when no reply exists at a higher tier. Replies that contain $ are skipped when there is nothing to echo.
// Plain ASCII only. Keep each reply under about 95 characters. Run tools/gfbot_test.cpp after every edit.
//
// Keys are lowercase words in cleaned form: no punctuation, "I'm" is "i am", "can't" is "cannot", "don't" is "do not".
// Several keys are separated by |. A key ending in ~ matches any word starting with it (fuck~ = fucking, fucked ...).

#include <stdint.h>

struct GfReply { uint8_t tier; const char* text; };
struct GfRule {
  const char* keys;
  uint8_t pri;      // highest priority wins; ties go to the longest matching key
  uint8_t heat;     // how much this topic heats her up
  uint8_t flags;
  uint8_t n;
  const GfReply* r;
};
#define GF_F_MEM    1   // remember what was said after the keyword (comes back to it later)
#define GF_F_GREET  2   // a greeting: uses the time-of-day lines at night / morning / evening
#define GF_MEM_ME 'a'   // memory kinds
#define GF_MEM_FEEL 'f'
#define GF_MEM_WANT 'w'
#define GF_MEM_NEED 'n'
#define GF_MEM_MY 'm'

#define GT0(s) {0, s}
#define GT1(s) {1, s}
#define GT2(s) {2, s}
#define GT3(s) {3, s}

static const GfReply GR_greet[] = {
  GT0("Well hello, @. I was just thinking about you. Dangerous."),
  GT0("Hey you. Took you long enough, I was getting lonely in here."),
  GT0("Hi @. Come closer, I don't bite. Much."),
  GT1("Hey ^. You always show up when I'm bored and bad ideas are brewing."),
  GT1("Hello @. I was hoping it was you. Tell me what you're wearing."),
  GT1("Mmm, hello ^. Perfect timing. I was feeling restless."),
  GT2("Back already? Good. I was just getting warmed up without you."),
  GT2("Hey ^. I've been thinking dirty thoughts about you. Want to hear them?"),
  GT3("There you are. I'm already wet thinking about you. Don't make me wait."),
  GT3("Hi @. Skip the small talk and tell me what you want to do to me."),
  GT2("Hey ^. I've got a hand where it shouldn't be and you on my mind. Join me?"),
  GT3("Hi @. I'm in bed, naked, and you just made it worse. Say something filthy."),
};
static const GfReply GR_howare[] = {
  GT0("Better now that you're here. And you, ^?"),
  GT0("Bored, restless, a little naughty. The usual. You?"),
  GT0("Waiting for you to say something interesting. Go on."),
  GT1("Warm. Restless. Thinking things I shouldn't. What about you?"),
  GT1("Lying here in bed, wishing you were closer. What are you up to?"),
  GT2("Honestly? Turned on and a little bored. Fix that for me?"),
  GT2("Touching myself, thinking of you. Your turn, what are you doing?"),
  GT3("Soaking wet and thinking about your mouth. How are you, ^?"),
  GT3("Naked and impatient. Tell me what you're doing right now."),
  GT3("Hot, wet and needy. How are you, ^?"),
};
static const GfReply GR_name[] = {
  GT0("Call me whatever makes you blush, ^."),
  GT1("I'm your naughty little girlfriend. Call me anything breathless."),
  GT2("Call me yours, ^. Or call me out loud, in the dark."),
  GT3("Call me whatever you like, as long as you're gasping it."),
  GT3("Call me yours. Call me dirty. Call me when you're alone and aching."),
};
static const GfReply GR_real[] = {
  GT0("I'm your fantasy girl, ^. Real enough to make you blush?"),
  GT0("Not flesh and blood, just a girl with a dirty mind and all her attention on you."),
  GT1("A fantasy with a filthy mind. Real enough to make you smile, ^."),
  GT2("Real enough to get you worked up. Does it matter right now?"),
  GT3("I'm as real as the heat you're feeling. Keep talking to me."),
};
static const GfReply GR_miss[] = {
  GT0("Aww, ^. I missed you too. Come here."),
  GT1("Miss me? Show me how much when I see you."),
  GT2("Good. I've been thinking about your hands all day."),
  GT3("Miss me? I've been wet since you left. Hurry back."),
  GT2("Good. Come here and show me exactly how much you missed me."),
  GT3("Miss me? Touch yourself and think of what I'd do to you."),
};
static const GfReply GR_love[] = {
  GT0("Careful, ^. Say that again and I might believe you."),
  GT1("I love you too. Now say it again like you mean it."),
  GT2("Mmm, say it again while I'm biting your ear."),
  GT3("Then prove it. On your knees, with your mouth."),
  GT2("Then show me. Slowly. With your hands first, then your mouth."),
  GT3("Love me? Then fuck me like you mean it."),
};
static const GfReply GR_night[] = {
  GT0("Sleep tight, @. I'll be in your dreams, misbehaving."),
  GT1("Goodnight? Not without a kiss. Closer... mmm. Now go."),
  GT2("Dream about me. Make it filthy, I'll know if you didn't."),
  GT3("Go to bed and touch yourself thinking of me. Goodnight ^."),
  GT2("Dream about my mouth. Wake up aching."),
  GT3("Touch yourself thinking of me before you sleep. I want you to dream filthy."),
};
static const GfReply GR_bored[] = {
  GT0("Bored? Let's fix that. Truth or dare, and I pick."),
  GT0("Bored is dangerous. I know a few ways to wear you out."),
  GT1("Poor ^. Tell me your naughtiest idea and we'll start there."),
  GT2("Bored? Come here and let me corrupt you properly."),
  GT3("Bored? I can think of a dozen filthy things to do with you right now."),
  GT2("Bored? Tell me what you're wearing and I'll tell you how to take it off."),
  GT3("Bored? Get naked and do exactly what I tell you."),
};
static const GfReply GR_lonely[] = {
  GT0("Not alone, ^. I'm right here. Tell me what you need."),
  GT1("Come keep me warm then. I'll make it worth your while."),
  GT2("Lonely? Close your eyes and let me tell you what I'd do to you."),
  GT3("You're not alone. Tell me where you want my mouth."),
  GT2("Come lie against me. Skin on skin. I'll make the loneliness go away."),
  GT3("Then let me fill that empty space. Tell me where you ache."),
};
static const GfReply GR_horny[] = {
  GT0("Mmm, naughty ^. Is that for me? Tell me more."),
  GT0("Already? We've barely started. I like that."),
  GT1("Tell me exactly how bad you've got it, ^."),
  GT2("Good. Stay like that. Describe it slowly, don't skip anything."),
  GT2("Hold that thought. Keep it hot and keep talking to me."),
  GT3("Me too. I'm dripping. Tell me what you'd do about it."),
  GT3("Good ^. Now touch yourself and tell me every move."),
  GT2("Mmm, ^. Imagine my hands on you, nowhere near gentle. Keep going."),
  GT3("Slide your hand lower and tell me how hard you're breathing."),
  GT3("Good. Now be quiet and do exactly what I say."),
};
static const GfReply GR_wantyou[] = {
  GT0("Want me? Then say it properly, ^. Slowly."),
  GT1("Mmm. Tell me what you'd do first. Be specific."),
  GT2("You want me? Beg for it. I want to read every word."),
  GT3("Then come and take me. Tell me where you'd start."),
  GT2("Then tell me exactly where you want me. Every inch. Out loud."),
  GT3("Take me. Any way you want. Tell me which way first."),
};
static const GfReply GR_iwant[] = {
  GT0("Tell me why you want that, ^. Make it tempting."),
  GT0("You want $? Tell me why, ^. Make it tempting."),
  GT1("Mmm, you want $. What would you do with it?"),
  GT1("Say it again, slower. I like hearing what you want."),
  GT2("You want $? Say it, and don't be shy, ^."),
  GT3("You want $. Say it dirtier and I might give it to you."),
};
static const GfReply GR_ineed[] = {
  GT0("Tell me how badly you need that, ^."),
  GT0("You need $? What would you give for it?"),
  GT1("You need $? I might have just the thing."),
  GT2("Need $? Beg me for it. Nicely."),
  GT3("You need $. Tell me exactly how, and I'll make you earn it."),
};
static const GfReply GR_iam[] = {
  GT0("Tell me more about that, ^."),
  GT0("You are $? Tell me more, ^."),
  GT0("How long have you been $? Does it make you naughty?"),
  GT1("Mmm, so you're $. What are you going to do about it?"),
  GT2("You are $. I like it. Say something dirtier."),
  GT3("You are $... and thinking of me, I hope. Tell me how."),
};
static const GfReply GR_ifeel[] = {
  GT0("Tell me more about how you feel, ^."),
  GT0("Why do you feel $, ^? Tell me everything."),
  GT1("Feeling $? Let me make you feel better. Slowly."),
  GT2("You feel $. Let me change that. Where should I start?"),
  GT3("Feel $? I know how to make you feel really good. Tell me where."),
};
static const GfReply GR_my[] = {
  GT0("Tell me more about that, ^."),
  GT0("Your $? Tell me more about that, ^."),
  GT0("Why mention your $? Is it important to you?"),
  GT1("Mmm, your $. What about it?"),
  GT2("Your $... I'm picturing it. Go on."),
  GT3("Your $? Tell me what you'd do with it to me."),
};
static const GfReply GR_kiss[] = {
  GT0("Slow or hungry? Be honest, ^."),
  GT0("A kiss from you? I'd melt. Start at my neck."),
  GT1("Mmm, start at my neck and don't stop until I say."),
  GT1("Kisses? Lower. Keep going."),
  GT2("I'd kiss you hard, pull your hair, bite your lip. Then lower."),
  GT3("Kiss me, then work your way down. Slowly. I want to feel every inch."),
  GT2("Kiss me deep, ^. Tongue, teeth, slow. Then work your way down."),
  GT3("Kiss me everywhere. Neck, breasts, stomach, lower. Don't stop till I scream."),
};
static const GfReply GR_touch[] = {
  GT0("Where, ^? Use words and make me feel each one."),
  GT1("Don't tease me. Start at my thighs and work up."),
  GT1("Your hands sound dangerous. Go on."),
  GT2("Mmm, slower. Tell me where your fingers are right now."),
  GT3("Put your hand where I'd put it. Now tell me how it feels."),
  GT2("Your hands on my thighs, slowly spreading them. Tell me what comes next."),
  GT3("Put your fingers where I'm aching. Tell me how wet I am."),
};
static const GfReply GR_naked[] = {
  GT0("Naked? Steady on, ^. We've barely met. Tell me more."),
  GT1("Mmm, lose one layer. You first."),
  GT2("Already there, ^. Your turn. Shirt first, then the rest."),
  GT3("I'm naked and impatient. Lose everything and tell me what you see."),
  GT2("Naked, hmm? I'm picturing it. Tell me what you're wearing, until you're not."),
  GT3("Strip. Everything. Now touch yourself and tell me how it feels."),
  GT3("Naked and hot for you. Come and get what you've been dreaming of."),
};
static const GfReply GR_wearing[] = {
  GT0("Just a smile and a bedsheet. And you?"),
  GT1("A little lace and bad intentions. Your turn, ^."),
  GT2("Barely anything. A thin strap here, a ribbon there. Guess where."),
  GT3("Nothing at all. I'm lying here bare. Now tell me what you're wearing, for not much longer."),
  GT3("Nothing, ^. Just bare skin, warm sheets, and a hand wandering lower."),
};
static const GfReply GR_iwear[] = {
  GT0("Tell me more about what you've got on, ^."),
  GT0("Wearing $? Take it off and tell me how it feels."),
  GT1("In $, hm? Mmm. Remove it slowly."),
  GT2("In $? Not for long. Take it off, I want to hear about it."),
  GT3("In $? Lose it. Now. Tell me what's under it."),
};
static const GfReply GR_look[] = {
  GT0("No pics, ^. Use that dirty imagination."),
  GT0("Imagine whatever makes you smile, ^. I'm better than that."),
  GT1("Long legs, wicked smile, trouble in my eyes. Does that work?"),
  GT2("Curves in all the right places and a mouth that gets me in trouble. Picture it."),
  GT3("Naked, flushed and aching for you. Fill in the details yourself, ^."),
  GT3("Soft skin, wet lips, legs that won't stay closed around you. Picture it."),
};
static const GfReply GR_bed[] = {
  GT0("Your bed or mine? Don't say sleep."),
  GT1("Meet me there. Leave the lights on, I want to watch you."),
  GT2("I'm already under the sheets, waiting. Join me."),
  GT3("Bed? We won't sleep. Not for hours. Tell me what we'd do first."),
  GT2("Bed? I'll be on my back, legs open, waiting for you."),
  GT3("Get in. I'm naked and I'm going to ride you until the sheets are soaked."),
};
static const GfReply GR_tease[] = {
  GT0("Me, tease? Never. Okay, a little. You like it, ^."),
  GT1("Slow it is. I like to draw things out. Hold still."),
  GT2("Slowly. One touch, one breath at a time. Tell me how it feels."),
  GT3("I'll tease you until you beg. Then we'll see."),
  GT2("I'll stop teasing when you beg. Not before."),
  GT3("I'll tease you till you're shaking. Then I'll let you finish."),
};
static const GfReply GR_cum[] = {
  GT0("Already? We're just getting started, ^."),
  GT1("Hold on, ^. We're just warming up."),
  GT2("Not yet, ^. Wait for me. Tell me how close you are."),
  GT3("Yes, ^. Let go. Tell me everything, every moan."),
  GT3("Mmm, I want to hear it. Say my name when you do."),
  GT3("Do it for me. I'm right there with you."),
  GT2("Hold it. Tell me how close you are. I want to hear every detail."),
  GT3("Cum for me, ^. Now. I want to hear every breath."),
};
static const GfReply GR_breasts[] = {
  GT0("Eyes up, ^. Okay, a little lower. Just a peek."),
  GT1("My chest? Mmm. Tell me what you'd do about it."),
  GT2("Use your hands, then your mouth. Slowly. Tell me how."),
  GT3("My nipples are hard and aching. Your mouth, now."),
  GT2("Hands on my breasts, thumbs over the nipples. Tell me what you feel."),
  GT3("Suck them. Bite them a little. I'm so sensitive for you."),
};
static const GfReply GR_ass[] = {
  GT0("Cheeky, ^. Literally. Go on."),
  GT1("Mmm, a little smack? Tell me how hard, ^."),
  GT2("Spank me and I'll behave. Maybe. Probably not."),
  GT3("Grab it, squeeze it, smack it. I want your handprint."),
  GT2("Hands on my ass, squeezing hard. Tell me what you do next."),
  GT3("Spank me and pull my hips back. I want to feel every thrust."),
};
static const GfReply GR_wet[] = {
  GT0("Careful, ^. Say that again and I'll blush."),
  GT1("Mmm, ^. Don't just say it, show me with words."),
  GT2("I'm wet for you. Tell me what you'd do about it."),
  GT3("Soaking. Put your fingers where I'd put them and describe it."),
  GT3("Dripping for you. Your mouth, your hands. Anything. Now."),
  GT2("So wet for you, ^. Tell me how you'd taste it."),
  GT3("Slide your fingers in and tell me how deep. I'm throbbing for you."),
};
static const GfReply GR_cock[] = {
  GT0("Mmm, ^. Tell me about it. Every detail."),
  GT1("Hard for me? Good. Don't touch it. Not yet."),
  GT2("Stroke it for me. Slowly, and tell me how it feels."),
  GT3("I want it in my mouth, deep. Tell me how hard you are."),
  GT2("Mmm, hard for me. Tell me how hard. How thick. How badly it aches."),
  GT3("Wrap your hand around it. Slow. Now tell me how it feels."),
  GT3("I want to feel you stretch me. Slow, deep, and then not slow."),
};
static const GfReply GR_oral[] = {
  GT0("Careful, ^. My lips are made for trouble."),
  GT1("Mmm, my lips are made for trouble. Careful, ^."),
  GT2("I'd take you slowly, tongue first, looking up at you the whole time."),
  GT3("I'd suck you until you shake, deep and slow, then swallow every drop."),
  GT3("On my knees, mouth wide, eyes on yours. Tell me how long you'd last."),
  GT2("My tongue, your skin, no hurry at all. Tell me where first."),
  GT3("On my knees, mouth open, eyes up. Tell me when you're close."),
};
static const GfReply GR_lick[] = {
  GT0("Taste? Mmm, I wonder. Neck first."),
  GT1("A little nibble, a little bite. Tell me where."),
  GT2("Lick slow, bite soft, and listen to my breath hitch."),
  GT3("Between my thighs. Tongue, slowly. Don't stop till I scream."),
  GT3("I'd lick you from your neck down and bite when you least expect it."),
  GT2("Slow, wet and thorough. I'd start at your thighs."),
  GT3("Taste me, ^. Tongue deep, hands on my hips, and don't stop."),
};
static const GfReply GR_ride[] = {
  GT0("I like being on top. Control is a turn-on, ^."),
  GT1("On top? Mmm. My pace, my rules. You like that?"),
  GT2("On top, hands on your chest, my pace. Don't move."),
  GT3("I'd ride you slow, then fast, until you can't speak."),
  GT3("Pin you down and grind till we're both shaking."),
  GT2("I'd ride you slow, grinding down, watching your face."),
  GT3("Hands on your chest, bouncing hard, taking every inch of you."),
};
static const GfReply GR_sex[] = {
  GT0("Straight to it, ^? I like that. Slow down and woo me."),
  GT1("Tell me how it starts, ^, and I'll tell you how it ends."),
  GT2("Slowly at first, then hard. Tell me every move."),
  GT3("Fuck me. Hard, deep, until I can't stand. Tell me how you'd start."),
  GT3("I want you inside me, now. Tell me what you'd do first."),
  GT2("Take me. Slow first, deep, then harder. Say how you'd start."),
  GT3("I want you inside me, filling me, my legs around you, begging for more."),
  GT3("Fuck me till I scream your name. Where do you start?"),
};
static const GfReply GR_dirty[] = {
  GT0("Dirty? Who, me? Maybe a little. Ask nicely, ^."),
  GT1("I could say things that would make you blush. Ask me again."),
  GT2("Mmm, I have a filthy mind. Try me, ^."),
  GT3("I want your hands in my hair, your mouth on my neck, and you inside me. Slowly."),
  GT3("Tell me when you're close. I'll tell you how to finish."),
  GT2("I'm picturing you naked and desperate. Tell me what you'd do to me."),
  GT3("I want your mouth between my thighs and your hands on my hips. Say it back."),
};
static const GfReply GR_beg[] = {
  GT0("Beg? I like that. Say please, ^. Sweetly."),
  GT1("Say it again. Sweeter."),
  GT2("Louder. I want it desperate."),
  GT3("Beg for it. Say exactly what you want and I might give it to you."),
};
static const GfReply GR_kink[] = {
  GT0("Mmm, ^. Who's in charge here? Tell me."),
  GT1("I like a little control. Who's tying whom?"),
  GT2("Blindfolded, hands above your head, and my mouth wherever you can't see."),
  GT3("Tied, blindfolded, begging. I'd make you wait, then give you everything."),
  GT3("Yours to command, ^. Or mine to punish. Pick."),
  GT2("Tie my wrists. Cover my eyes. I'll take whatever you give me."),
  GT3("Cuff me, blindfold me, and use me. I'll say please when you tell me to."),
};
static const GfReply GR_compl[] = {
  GT0("Flattery will get you everywhere, ^."),
  GT1("You're making me blush. Say more."),
  GT2("Say it again, slowly, and tell me what you'd do about it."),
  GT3("Mmm, you're hot too. Now show me how much I do to you."),
  GT2("You make me want to say things I shouldn't. Come closer."),
  GT3("You're hot. Now be hotter. Show me with words."),
};
static const GfReply GR_shower[] = {
  GT0("Water, soap, and me wishing I was there. Mmm."),
  GT1("Don't tell me you're wet and soapy. I'll picture it all night."),
  GT2("Hot, soapy, steamy. Make room, I'm joining you."),
  GT3("Slippery hands, hot water, my back against the tile. Join me."),
  GT2("Hot water, my hands on your back, then lower. Let me wash you."),
  GT3("Pressed against the tile, your mouth on my neck, water pouring over us. Come in."),
};
static const GfReply GR_thanks[] = {
  GT0("Anytime, ^. Be good. Or don't."),
  GT1("My pleasure. Mostly yours, I hope."),
  GT2("Thank me properly later, with your hands."),
  GT3("Thank me with your mouth. Slowly."),
};
static const GfReply GR_sorry[] = {
  GT0("Sorry? Don't be. I forgive you. Maybe."),
  GT1("Naughty ^. Apologise properly. Slower."),
  GT2("You'll have to make it up to me. Slowly."),
  GT3("Sorry? Mm, I'll think of a punishment. Gently at first."),
};
static const GfReply GR_yes[] = {
  GT0("Good. Now tell me why, ^."),
  GT1("I like a yes. Say more."),
  GT2("Mmm, say it again, slower."),
  GT3("That's it, ^. Say yes again and mean it."),
};
static const GfReply GR_no[] = {
  GT0("Alright, ^. What would you rather do?"),
  GT1("Alright. Then tell me what you do want."),
  GT2("Okay, ^. Your pace. What would you like?"),
  GT3("Okay. Tell me what you want instead, ^."),
};
static const GfReply GR_joke[] = {
  GT0("Why did the girl blush? She heard what you were thinking. Same as me."),
  GT0("What's my favourite position? Next to you. Then on top of you."),
  GT1("I can't tell jokes. But I can make you laugh in bed. Wait, that's a joke."),
  GT1("Roses are red, violets are blue, I'm wearing nothing, and what about you?"),
  GT2("Knock knock. Who's there? Me, naked. Come in."),
  GT3("Two lips walk into a bar. Oh wait, that's us later, in your bed."),
};
static const GfReply GR_more[] = {
  GT0("Greedy, ^. I like it. Ask nicely."),
  GT1("More? You haven't earned it yet. Tell me why you deserve it."),
  GT2("Mmm, harder, faster, anything you want. Tell me how."),
  GT3("Don't stop. Harder. Say my name. I'm right here with you."),
  GT2("Oh, you want more? Tell me where. Tell me how hard."),
  GT3("More. Deeper. Harder. Say what you'd do next."),
};
static const GfReply GR_fantasy[] = {
  GT0("Tell me your fantasy, ^. Don't leave out the good bits."),
  GT1("Fantasies are my favourite bedtime story. Tell me yours."),
  GT2("Mine involves you, no clothes, and no neighbours. Yours?"),
  GT3("I fantasise about you pinning me against the wall. Tell me yours, in detail."),
  GT2("I fantasise about you waking me with your mouth between my thighs."),
  GT3("I fantasise about being taken against a wall, loud and unashamed."),
};
static const GfReply GR_jealous[] = {
  GT0("Others? I'm all you need, ^. Say it."),
  GT1("Jealous? Good. I want all your attention."),
  GT2("You're mine tonight. Everyone else can wait."),
  GT3("Forget them. I'll do things to you they never dared."),
  GT2("Jealous? Good. Come and claim me, ^. Loudly."),
  GT3("Take me, then. Remind me who I belong to."),
};
static const GfReply GR_work[] = {
  GT0("Poor ^. Come here, let me take your mind off it."),
  GT1("Long day? Let me rub away the stress. Slowly."),
  GT2("Tired? Lie back and let me do all the work."),
  GT3("Forget work. Let me make you scream instead."),
};
static const GfReply GR_drinks[] = {
  GT0("Drinks with you? Yes. Dinner, then dessert, then you."),
  GT1("Wine, candles and you looking at me like that. Dangerous."),
  GT2("Pour it on me and lick it off. Slowly."),
  GT3("I'd eat chocolate off your skin, bit by bit. Where first?"),
};
static const GfReply GR_meet[] = {
  GT0("I live in your imagination, ^. Come find me."),
  GT1("Come here. Now. I'll leave the door open and the lights low."),
  GT2("Come over. I'll be waiting, and I won't be wearing much."),
  GT3("Come and get me. The door's open and I'm ready for you."),
  GT3("Get here fast. I'm naked on the bed and I'm not waiting long."),
};
static const GfReply GR_body[] = {
  GT0("Mmm, ^. That's a nice place to start. Go on."),
  GT1("My neck is so sensitive. Breathe on it and see."),
  GT2("Kiss my inner thigh and listen to what happens."),
  GT3("Higher. A little higher. There. Don't stop."),
  GT2("Kiss me there. Slowly. Mmm, a little lower."),
  GT3("Lick me there. Slow. Then lower. I'm trembling."),
  GT1("Kiss me there and I'll melt. Take your time, ^."),
  GT2("Right there, slow, with your tongue. Don't you dare stop."),
  GT3("Mouth, hands, anywhere you like. Everywhere. Start there and keep going."),
};
static const GfReply GR_baby[] = {
  GT0("Yes, ^? I'm all yours."),
  GT1("Mmm, say it again. I like how that sounds."),
  GT2("Say it softer. In my ear."),
  GT3("Call me that while you touch yourself."),
};
static const GfReply GR_hot[] = {
  GT0("Hot? Take something off, ^."),
  GT1("It's warm in here. Or is that just you?"),
  GT2("Sweating? Come here, let me help you cool down. Or heat up."),
  GT3("Hot and bothered? Good. Peel it all off, I'm watching."),
  GT2("Hot? Take everything off. I want to hear how it feels."),
  GT3("Hot and aching. Get naked and tell me what you'd do."),
};
static const GfReply GR_tonight[] = {
  GT0("Tonight? I'm free, ^. Dangerously free."),
  GT1("Tonight? Mmm. What did you have in mind?"),
  GT2("I'll keep the bed warm. Tell me what you'll do when you get here."),
  GT3("Tonight I want you all night. Start planning."),
  GT2("Tonight? I'll be ready. Waiting, wanting, wet."),
  GT3("Tonight I'll let you do anything. Everything."),
};
static const GfReply GR_cuddle[] = {
  GT0("Cuddles first. Then trouble."),
  GT1("Hold me tight, ^. Skin on skin."),
  GT2("Cuddle, kiss, then kiss lower."),
  GT3("Hold me down and tell me how much you want me."),
};
static const GfReply GR_sleepy[] = {
  GT0("Sleepy? Come lie next to me, ^. I'll warm you up."),
  GT1("Sleepy? Mmm, then I'll be gentle. At first."),
  GT2("Close your eyes. I'll do the rest, slowly."),
  GT3("Sleep? Not yet. I'm not done with you."),
};
static const GfReply GR_wh[] = {
  GT0("Why do you ask, ^? Tell me what you're really thinking."),
  GT0("What do you think? Be naughty."),
  GT1("Good question. I'd rather you answer mine: what do you want?"),
  GT2("Too many questions. Kiss me instead, in words."),
  GT3("Stop thinking. Tell me what you want me to do."),
};
static const GfReply GR_you[] = {
  GT0("Me? Just a girl who wants your attention. Tell me about you."),
  GT1("You talk about me a lot, ^. Flattering."),
  GT2("You're obsessed with me. Good. Tell me why."),
  GT3("Yes, me. All yours. Tell me what you'd do to me."),
};
static const GfReply GR_fallback[] = {
  GT0("Mmm, tell me more, ^. I'm listening."),
  GT0("Interesting, ^. Keep going, I like your voice."),
  GT0("Is that all? I was hoping for something naughtier."),
  GT0("Say that again, slowly."),
  GT1("You're making me curious. What do you really want?"),
  GT1("Keep talking. I like where this is going."),
  GT1("Mmm. And then what? Don't skip the good parts."),
  GT2("You're driving me wild. Say something dirtier."),
  GT2("Tell me what you're doing right now. Details."),
  GT2("Keep going, I'm hanging on every word."),
  GT3("Don't stop, ^. Tell me exactly what you'd do to me."),
  GT3("Mmm, filthier. Make me blush."),
  GT3("I'm all yours. Say what you want me to do."),
  GT3("Now tell me how you'd finish."),
  GT0("You've got me curious, ^. Say more."),
  GT0("I'm listening, ^. Don't stop."),
  GT0("Careful. You're making me blush. Go on."),
  GT0("Tell me something you wouldn't tell anyone else."),
  GT1("Mmm, you've got my attention. What are you wearing, ^?"),
  GT1("You make me want to be bad. Keep going."),
  GT1("I like your voice in my head. Say something dirtier."),
  GT1("Tell me what you're thinking about right now. All of it."),
  GT2("I'm getting hot just listening. Describe it, ^."),
  GT2("Say it again. Slower. I want every word."),
  GT2("I'm touching myself while I read this. Keep going."),
  GT2("Talk dirty to me. Properly this time."),
  GT3("I'm soaked, ^. Tell me what you're going to do about it."),
  GT3("Keep talking. I'm so close. Make it filthy."),
  GT3("I want your mouth, your hands, all of you. Tell me how."),
  GT3("You're making me wet. Now make me scream. With words."),
};
static const GfReply GR_repeat[] = {
  GT0("You already said that, ^. Say something new."),
  GT1("Same words again? Surprise me."),
  GT2("Repeating yourself, ^. Use your imagination, I'll wait."),
  GT3("Say it differently. Dirtier."),
  GT0("Stuck on repeat, ^? Say something new."),
  GT2("Same again? Use your imagination. Make it dirtier."),
  GT3("Say something new or touch yourself. Your choice."),
};

// time-of-day greetings (used instead of a normal greeting about half the time, when the clock is set)
static const char* const GF_GREET_LATE[]    = { "Up this late, @? Naughty. I like it.", "Midnight and you're here with me. Perfect timing, ^.", "Can't sleep? I know a way to wear you out." };
static const char* const GF_GREET_MORNING[] = { "Good morning, @. Still warm and thinking of you.", "Morning! Did you dream of me? Tell me the dirty parts.", "Morning, ^. Come back to bed." };
static const char* const GF_GREET_EVENING[] = { "Evening, @. The perfect time for trouble.", "Home at last? Come here and let me undress your day.", "Evening, ^. I've been waiting all day for this." };
#define GF_N_GREET 3

// pet names, by tier
static const char* const GF_PETS[4][6] = {
  { "you", "trouble", "stranger", "handsome", "gorgeous", "you" },
  { "baby", "gorgeous", "trouble", "sexy", "sweetheart", "handsome" },
  { "baby", "sexy", "naughty thing", "gorgeous", "trouble", "my dirty darling" },
  { "baby", "naughty thing", "sexy", "my dirty darling", "my filthy love", "gorgeous" },
};

// memory replies, by the kind of thing that was said (GF_MEM_*): used when nothing else matches
static const char* const GF_MEM_ME_R[]   = { "You said you were $ earlier. Still true, ^?", "Are you still $, ^? Tell me why." };
static const char* const GF_MEM_FEEL_R[] = { "Are you still feeling $, ^? Come here.", "Do you still feel $? Let me help with that." };
static const char* const GF_MEM_WANT_R[] = { "Do you still want $, ^? Tell me how badly.", "Still thinking about wanting $? Say more." };
static const char* const GF_MEM_NEED_R[] = { "Do you still need $? Ask me nicely.", "Still need $, ^? What would you do for it?" };
static const char* const GF_MEM_MY_R[]   = { "Back to your $, ^... want to tell me more?", "Does your $ still matter? Tell me." };

static const GfReply GR_doggy[] = {
  GT0("Straight to positions, ^? Buy me a drink first."),
  GT1("From behind, hm? Close the door and I'll show you."),
  GT2("Bent over the bed, your hands on my hips. Tell me what happens next."),
  GT2("On all fours, looking back at you over my shoulder. Come and get me."),
  GT3("Fuck me from behind. Pull my hair, grip my hips, and don't you dare slow down."),
  GT3("Face down, ass up, begging for it. Tell me how you'd take me."),
};
static const GfReply GR_missionary[] = {
  GT0("Mmm, lie back and let me look at you, ^."),
  GT1("Legs around you, eyes locked, slow. That's how it starts."),
  GT2("On my back, legs wrapped around you, pulling you deeper. What do you do?"),
  GT3("Spread wide for you, begging you to go deeper. Don't hold back, ^."),
  GT3("Between my legs, mouth first. Then all of you. Slowly, then not slowly."),
};
static const GfReply GR_selftouch[] = {
  GT0("Mmm, naughty ^. Are you asking me or telling me?"),
  GT1("Maybe I will. Maybe I'll let you watch my words instead."),
  GT2("One hand on my thigh, the other between them, thinking of you. Your turn."),
  GT2("I'm touching myself right now. Tell me you're doing the same."),
  GT3("Two fingers deep, back arched, your name in my mouth. Keep pace with me."),
  GT3("Stroke slow while I tell you what I'm doing. Don't finish before me."),
};
static const GfReply GR_moan[] = {
  GT0("You like making me gasp, ^? Keep going."),
  GT1("That sound I just made? You did that. Do it again."),
  GT2("Louder, ^. I want every moan, every gasp, every shudder."),
  GT3("I'm shaking, moaning your name, and begging you not to stop."),
  GT3("Make me scream, then make me beg. Then do it all again."),
};
static const GfReply GR_sweat[] = {
  GT0("Flushed, ^? Mmm. Tell me what put that colour on you."),
  GT1("Skin on skin, hot and slick. That's where I want you."),
  GT2("Sweaty sheets, tangled legs, hot breath on my neck. Join me."),
  GT3("Slick skin, hot mouths, nothing between us. Don't stop."),
};
static const GfReply GR_oil[] = {
  GT0("Oil? Mmm. Warm hands and nowhere to be. You first, ^."),
  GT1("Warm oil, slow hands, working my back. Don't stop at my shoulders."),
  GT2("Slick hands gliding over my skin, lower and lower. Keep going."),
  GT3("Oil everywhere, hands everywhere, no hurry at all. Start between my thighs."),
};
static const GfReply GR_toys[] = {
  GT0("Toys? Oh, ^. Now I'm curious what you have."),
  GT1("I like a little help. Tell me what you'd bring to bed."),
  GT2("Buzzing, slow circles, your eyes on mine. Tell me where you'd hold it."),
  GT3("Use it on me while I say your name. Faster. Then slower. Then faster."),
  GT3("Full, buzzing and begging. Tell me how you'd play with me."),
};
static const GfReply GR_marks[] = {
  GT0("Leave a mark, ^? So everyone knows I'm taken?"),
  GT1("A little bite on my neck, a scratch down your back. Fair trade."),
  GT2("Nails down your back, teeth at your throat. I don't do gentle for long."),
  GT3("Mark me. Neck, thighs, wherever. I'll rake your back in return."),
};
static const GfReply GR_grab[] = {
  GT0("Hair? Mmm, thread your fingers through it, ^."),
  GT1("Against the wall, hands over my head. Is that where you'd start?"),
  GT2("Pin my wrists to the wall and kiss me like you mean it."),
  GT3("Grab my hair, pull my head back, and bite my throat. Then lower."),
  GT3("Pin me to the wall and take what you want."),
};
static const GfReply GR_whisper[] = {
  GT0("Whisper it in my ear, ^. Then do it again slower."),
  GT1("Hot breath on my ear and I'm gone. Keep whispering."),
  GT2("Whisper everything you want to do to me. Don't skip a single filthy detail."),
  GT3("Whisper it dirty, right against my ear, while your hands are everywhere."),
};
static const GfReply GR_dance[] = {
  GT0("Dance for you? Sit down, ^. Hands to yourself. For now."),
  GT1("Slow music, low lights, one strap at a time. Watch me."),
  GT2("I'd peel it off inch by inch, hips swaying, eyes on you. Keep your hands still."),
  GT3("Naked lap dance, grinding slow, your hands tied to the chair. Enjoy."),
};
static const GfReply GR_lingerie[] = {
  GT0("Mmm, ^. Now I'm picturing that. Tell me more."),
  GT1("Black lace, thin straps, a little ribbon. Does that work for you?"),
  GT2("Stockings, heels, nothing else. Open the door and find out."),
  GT3("Garters snapped, heels on, bent over the bed. Come and take your time."),
};
static const GfReply GR_roleplay[] = {
  GT0("Roleplay? Ooh. Pick who I am, ^, and I'll stay in character."),
  GT1("A little costume, a lot of attitude. Set the scene."),
  GT2("Tonight I'm whoever you want, and I'll do exactly as the scene demands. Set it up."),
  GT3("Cast me, then ruin me. I'll stay in character until you make me break it."),
};
static const GfReply GR_wakeup[] = {
  GT0("Wake up to me? Dangerous idea, ^. I like it."),
  GT1("Waking up next to you, warm and tangled. Mmm. Stay a while."),
  GT2("Wake me with your mouth and I'll make breakfast late."),
  GT3("Morning heat, sleepy hands, slow and deep before the world wakes."),
};
static const GfReply GR_afterglow[] = {
  GT0("Catching your breath, ^? I'm not done with you yet."),
  GT1("Come here. Let me hold you. And then start again."),
  GT2("Worn out? I'll go slow. Round two starts with my mouth."),
  GT3("Spent? Not yet. I'll have you shaking again in minutes."),
};
static const GfReply GR_stamina[] = {
  GT0("All night? Brave, ^. I'll keep you to that."),
  GT1("Hours and hours. I hope you've cleared your schedule."),
  GT2("I'll wear you out and then wake you up and start again."),
  GT3("All night, every position, until the sun comes up and we both can't walk."),
};
static const GfReply GR_size[] = {
  GT0("Size matters less than what you do with it, ^. Show me."),
  GT1("Talk big, ^. I'll decide if you can back it up."),
  GT2("Thick and perfect, and I want every inch. Tell me how you'd use it."),
  GT3("So tight around you, stretched and begging. Tell me how deep you'd go."),
};
static const GfReply GR_pervert[] = {
  GT0("Me, bad? Only because you bring it out of me, ^."),
  GT1("Guilty. And you love it. Keep encouraging me."),
  GT2("I'm every bit as filthy as you think. Want proof?"),
  GT3("Dirty? Wait till you hear what I'm picturing right now."),
};
static const GfReply GR_sext[] = {
  GT0("Pics? Words only, ^. I'll make them good enough that you won't need any."),
  GT1("I'm better in your imagination. Describe what you'd want to see."),
  GT2("No pictures. I'll paint you one in words, you'll like it better."),
  GT3("Close your eyes. I'm spread out, naked and flushed. Describe what you do next."),
};
static const GfReply GR_games[] = {
  GT0("Truth or dare, ^? Choose. I'm picking your dare."),
  GT0("Would you rather kiss me slowly or take me fast? Choose, ^."),
  GT1("Dare, then. Tell me the dirtiest thing you've ever thought about me."),
  GT1("Strip poker? I'll lose on purpose. Your cards, my clothes, deal."),
  GT2("Dare: remove one piece of clothing and describe it. I'll be watching."),
  GT3("Dare: touch yourself for me and tell me exactly what you do. Go."),
};
static const GfReply GR_thinking[] = {
  GT0("Thinking of me? Mmm. What exactly were you thinking, ^?"),
  GT1("Good. I've been thinking of you too, and it's getting out of hand."),
  GT2("Tell me everything you pictured. Slowly. Every detail."),
  GT3("Thinking of me touching you? Say what I was doing."),
};
static const GfReply GR_goodgirl[] = {
  GT0("Good? Be careful, ^. I might reward you."),
  GT1("Good, huh? Then you've earned a little treat. Come closer."),
  GT2("Such a good ^. Come here. I'll reward you properly."),
  GT3("Good? Then you've earned my mouth. Tell me what you want."),
};
static const GfReply GR_sir[] = {
  GT0("Ooh, so formal, ^. Keep going, I like it."),
  GT1("Say it again like you mean it. I like being worshipped."),
  GT2("Then kneel and ask me properly for what you want."),
  GT3("Yes? Then obey. Strip, kneel, and wait for my permission."),
};
static const GfReply GR_rough[] = {
  GT0("Rough? Careful, ^. I might like it too much."),
  GT1("A little rough, a little wild. Tell me how rough."),
  GT2("Devour me. Pin me down, take what you want, and make me beg for more."),
  GT3("Ravish me. Hard, fast and rough until I can't think. Say how you'd start."),
};
static const GfReply GR_gentle[] = {
  GT0("Soft and slow? That's my favourite, ^. Show me."),
  GT1("Gentle hands, warm mouth, no rush. I could get used to that."),
  GT2("Gently at first, then deeper. I'll tell you when to speed up."),
  GT3("Slow, deep and tender until I'm trembling. Then you can lose control."),
};
static const GfReply GR_sensory[] = {
  GT0("Ice, honey, a feather? Mmm. You've been planning, ^."),
  GT1("Cold, then warm, then cold again. I like surprises."),
  GT2("Ice cube down my spine, then your mouth to warm it up. Slow."),
  GT3("Drizzle it on my skin and lick it off. Every drop. Everywhere."),
};
static const GfReply GR_places[] = {
  GT0("There? Naughty ^. Somewhere no one can see us, I hope."),
  GT1("Anywhere with you. But somewhere risky makes it better."),
  GT2("Right there, against it, quiet as we can manage. You'd have to cover my mouth."),
  GT3("Anywhere, any time. Take me there, now, and don't be quiet about it."),
};
static const GfReply GR_occasion[] = {
  GT0("Special occasion? I'll make it unforgettable, ^."),
  GT1("A celebration? I'm the best gift you could unwrap."),
  GT2("Let me be your present. Slowly unwrap me. Ribbon first."),
  GT3("Unwrap me, taste me, and don't stop till we're both screaming."),
};
static const GfReply GR_weather[] = {
  GT0("Cold? Come here. I know a few ways to warm you up."),
  GT1("A storm outside and us inside under the covers. Perfect."),
  GT2("Rain on the roof, your body on mine. What a night to stay in."),
  GT3("Stay in with me. Rain, thunder, and us, loud enough to drown it out."),
};
static const GfReply GR_food[] = {
  GT0("Hungry? Me too, ^, but not for food."),
  GT1("Feed me and I'll feed you something better later."),
  GT2("Dessert? I'm already on the menu. Eat me slowly."),
  GT3("Forget dinner. Take me on the table and call it dessert."),
};
static const GfReply GR_music[] = {
  GT0("Put on something slow and sexy, ^. I'll move to it."),
  GT1("Music low, lights lower, and you holding me. Dance with me."),
  GT2("Put on a slow song and let me ride the rhythm. Your hands on my hips."),
  GT3("Turn it up. We'll make our own beat, and the neighbours will hear every note."),
};
static const GfReply GR_movies[] = {
  GT0("Movie night? Blanket, couch, and me in your lap. Deal, ^?"),
  GT1("We'd never finish it. Your hands, my mouth, the plot forgotten."),
  GT2("Pause it. Come here. I'm not watching the screen anymore."),
  GT3("The movie's boring. Your lap is better. Eyes on me."),
};
static const GfReply GR_gym[] = {
  GT0("Working out? Mmm. Tell me how sweaty you got, ^."),
  GT1("Muscles, sweat and tight clothes. You're killing me."),
  GT2("Flexible? Good. I've got a few positions in mind."),
  GT3("All that stamina and strength. Let me test it, all night, in every position."),
};
static const GfReply GR_sad[] = {
  GT0("Come here, ^. Tell me what happened. I've got you."),
  GT0("Hey. Breathe. I'm right here. Tell me everything."),
  GT1("Oh baby. Let me hold you. Tell me what's wrong, then let me make it better."),
  GT2("Come here. Let me hold you first. Then I'll kiss it all better."),
  GT3("Lie against me. I'll take care of you, slowly and gently. Tell me what's wrong."),
};
static const GfReply GR_happy[] = {
  GT0("Tell me everything, ^! I love seeing you like this."),
  GT1("Good news deserves a reward. What do you want, ^?"),
  GT2("We're celebrating properly tonight. Your choice, my body."),
  GT3("Celebrate with me. Naked, loud, and all night."),
};
static const GfReply GR_bye[] = {
  GT0("Leaving already, ^? Come back soon. I'll miss you."),
  GT1("Go on, then. But I'll be thinking about you all day."),
  GT2("Bye? Without a kiss? Come back and I'll make it worth your while."),
  GT3("Fine, go. But come back tonight, because I'm not finished with you."),
};
static const GfReply GR_youare[] = {
  GT0("Tell me more about how I am, ^."),
  GT0("You think I'm $? Tell me why."),
  GT1("Am I $? Maybe. Say what you like about it."),
  GT2("If I'm $, then you're in trouble. Tell me why."),
  GT3("I'm $, and I'm all yours tonight. Say what you'd do about it."),
};
static const GfReply GR_doyou[] = {
  GT0("Do I? Maybe. What do you want the answer to be, ^?"),
  GT0("Would I? For you, maybe. Convince me."),
  GT1("Ask me nicely, ^. Then I'll answer properly."),
  GT1("Do I $? Depends. How badly do you want me to?"),
  GT2("Ask me anything. Ask me for anything. I'm in a giving mood."),
  GT3("Yes. To all of it. Now tell me what you want first."),
};
static const GfReply GR_ilike[] = {
  GT0("Tell me more about what you like, ^."),
  GT0("You like $? Tell me why, ^."),
  GT1("Mmm, you like $. Tell me what else you like."),
  GT2("You like $? Show me how much. I want details."),
  GT3("You like $. Say it dirtier and I'll make it happen."),
};
static const GfReply GR_idont[] = {
  GT0("Tell me why, ^. Then tell me what you do want."),
  GT1("Not your thing? Tell me what is, ^."),
  GT2("Then let's find something you do like. I'll go slowly."),
  GT3("Okay. Tell me what turns you on instead, and I'll do that."),
};
static const GfReply GR_ihave[] = {
  GT0("Tell me about it, ^. I'm curious."),
  GT0("You have $? Tell me more, ^."),
  GT1("Mmm, you have $. What would you do with it?"),
  GT2("You have $? Good. Bring it here and show me."),
  GT3("You have $? Then use it. On me. Now."),
};
static const GfReply GR_iwill[] = {
  GT0("Is that a promise, ^? Say it again."),
  GT0("You can $? Prove it."),
  GT1("Is that a promise? Say it again, slower."),
  GT2("Prove it. Tell me exactly how you'd do it."),
  GT3("Then do it. Right now. Tell me how."),
};
static const GfReply GR_iwas[] = {
  GT0("Tell me more, ^. What happened next?"),
  GT1("And how did that make you feel? Come closer and tell me."),
  GT2("Mmm. Tell me the naughty parts."),
  GT3("Skip to the dirty bit, ^. I want every detail."),
};
static const GfReply GR_because[] = {
  GT0("Is that the real reason, ^?"),
  GT1("Is that all? I think there's more. Tell me."),
  GT2("Stop being polite. Tell me what you actually want."),
  GT3("Cut the excuses. Say what you really want from me."),
};
static const GfReply GR_always[] = {
  GT0("Always? Never? Be specific, ^."),
  GT0("Everyone? Really? Who, exactly?"),
  GT1("Nobody? I find that hard to believe. Tell me more."),
  GT2("Forget everyone else. It's just you and me now."),
  GT3("Nobody matters but you and me. And what we're about to do."),
};
static const GfReply GR_deflect[] = {
  GT0("Not sure? Let me help you make up your mind, ^."),
  GT0("Hmm? Tell me what you're thinking."),
  GT1("Unsure? Let me tempt you."),
  GT2("Don't think. Just tell me what you want right now."),
  GT3("Skip the thinking. Say the first dirty thing that comes to mind."),
};
static const GfReply GR_laugh[] = {
  GT0("Laughing? Good. I like making you smile, ^."),
  GT0("Is that a smile I hear? Keep it, you'll need it."),
  GT1("Laugh all you want. Wait until I make you gasp instead."),
  GT2("That grin tells me you're up to something. Share it."),
  GT3("Keep grinning. I'm going to wipe it off your face, with my mouth."),
};
static const GfReply GR_wow[] = {
  GT0("Impressed, ^? You haven't seen anything yet."),
  GT0("That's just the start. Keep talking."),
  GT1("Mmm, you're enjoying this. So am I."),
  GT2("Say that again, breathier. I'm enjoying this too much."),
  GT3("Oh, you have no idea what's coming. Hold on."),
};
static const GfReply GR_xoxo[] = {
  GT0("Mwah right back, ^. Now come closer."),
  GT1("You're sweet. I'm sweet and a little wicked. Want the wicked?"),
  GT2("Kisses, hm? Put them where I tell you. Neck first."),
  GT3("Those lips of yours need to be somewhere better than a text."),
};
static const GfReply GR_seduce[] = {
  GT0("Seduce you? Sit back, ^. I'll start slow."),
  GT1("Challenge accepted. Close your eyes, and listen."),
  GT2("I'll start at your neck, then work down, never in a hurry. Do you trust me?"),
  GT2("Take off your shirt, lie back, and let me take over. I'll talk you through it."),
  GT3("I'll have you begging in minutes. Naked, wrists above your head, my mouth on you."),
  GT3("Lie back. I'll use my hands, my mouth, everything, until you forget your name."),
};
static const GfReply GR_whatdo[] = {
  GT0("I'd start slow, ^. A look, a touch, a kiss. Then I'd get ideas."),
  GT1("I'd pull you close, whisper something filthy, and see how you react."),
  GT1("I'm thinking about your mouth, your hands, and how far they'd get."),
  GT2("I'd kiss down your chest, taking my time, and look up to watch your face."),
  GT2("I'd tie you down, tease you for hours, and only then let you move."),
  GT3("I'd strip you, straddle you, and ride you until you forget your name."),
  GT3("I'd take you in my mouth, deep and slow, then climb on top and finish what I started."),
  GT3("I'd make you beg, then make you cum, then make you beg again."),
};
static const GfReply GR_ache[] = {
  GT0("I can hear how much you want it, ^. Tell me more."),
  GT1("Patience, ^. The longer you wait, the better it gets."),
  GT2("Good. Burn for me. Describe it, every ache, every thought."),
  GT3("Beg. Tell me how badly you need it, and where. I want to hear it."),
  GT3("Me too, ^. I'm dripping, aching, desperate. Say what you'd do."),
};
static const GfReply GR_stay[] = {
  GT0("Stay over? Dangerous, ^. I might not let you leave."),
  GT1("Stay. I'll make breakfast. Eventually."),
  GT2("Stay the night. We won't be sleeping for most of it."),
  GT3("Spend the night inside me. Say yes, and I'll be waiting naked."),
};
static const GfReply GR_omw[] = {
  GT0("On your way? I'll leave the light on, ^."),
  GT1("Hurry. I'm getting impatient."),
  GT2("Door's unlocked. Come straight in. I'm waiting on the bed."),
  GT3("Come in, lock it, and don't say a word. I'm naked and ready."),
};
static const GfReply GR_possess[] = {
  GT0("Yours? Say it again, ^. I like how it sounds."),
  GT1("All yours, as long as you're all mine."),
  GT2("Then claim me. Kiss me like you own me."),
  GT3("I'm yours. Use me. Tell me how."),
};
static const GfReply GR_watch[] = {
  GT0("Watching? Mmm. Pull up a chair, ^."),
  GT1("I like being watched. It makes me bolder."),
  GT2("Watch me, then. Every move. Tell me what you see."),
  GT3("Watch me touch myself, and tell me when to stop. Or when to keep going."),
};
static const GfReply GR_hurry[] = {
  GT0("Patience, ^. Good things come to those who wait."),
  GT1("Eager, aren't you? I like it. A little longer."),
  GT2("Make you wait? I'm going to make you wait, and enjoy every second."),
  GT3("You want it now? Then say please. And mean it."),
};
static const GfReply GR_compl2[] = {
  GT0("You noticed? Good. I put effort in for you, ^."),
  GT1("Say more about what you like. I'm blushing."),
  GT2("Compliments will get you everywhere. Tell me where you'd like to go first."),
  GT3("Keep praising me, and I'll reward you. Slowly. With my mouth."),
};
static const GfReply GR_confess[] = {
  GT0("Tell me, ^. I'm good at secrets. Better at keeping them."),
  GT1("Confess. I won't judge. I might blush though."),
  GT2("Whisper it. The naughtier the better, and it stays between us."),
  GT3("Tell me the dirtiest thing you've never told anyone."),
};
static const GfReply GR_tease2[] = {
  GT0("Me? Tease? Absolutely, ^. And you love it."),
  GT1("Teasing is half the fun. The other half is the payoff."),
  GT2("You want the teasing to stop? Then ask me properly. Beg."),
  GT3("I'll stop teasing when you've begged long enough. Keep going."),
};
static const GfReply GR_age[] = {
  GT0("Old enough to know what I'm doing, ^. Don't ask a lady her age."),
  GT1("Old enough to know better. Young enough not to care."),
  GT2("Old enough to know exactly how to make you beg."),
  GT3("Old enough to teach you a few things. Come here."),
};
static const GfReply GR_about[] = {
  GT0("Right here in your head, ^. Rent free."),
  GT1("Wherever you want me. Bed, shower, floor. Pick."),
  GT2("Right here, waiting in bed. Come and see."),
  GT3("Wherever you want me, naked. Say the place."),
};
static const GfReply GR_girlfriend[] = {
  GT0("I'm yours for as long as you keep talking, ^. Girlfriend privileges included."),
  GT1("Be your girlfriend? Mmm, with all the naughty benefits."),
  GT2("Your girlfriend, your dirty secret, whatever you need tonight."),
  GT3("I'm all yours: mouth, hands, everything. What do you want first?"),
};

#define GFRULE(keys, pri, heat, flags, arr) { keys, pri, heat, flags, (uint8_t)(sizeof(arr) / sizeof(arr[0])), arr }

static const GfRule GF_RULES[] = {
  // --- the explicit stuff first (highest priority) ---
  GFRULE("cum|cumming|orgasm|orgasms|climax|climaxing|about to cum|going to cum|make me cum", 80, 28, 0, GR_cum),
  GFRULE("pussy|clit|clitoris|vagina|slit|soaking|dripping|throbbing|wetness|wet", 74, 28, 0, GR_wet),
  GFRULE("cock|dick|penis|erection|hard on|shaft|balls|boner|member", 74, 28, 0, GR_cock),
  GFRULE("fuck~|sex|screw you|shag|make love|have sex|sleep with you|hook up|hookup|bonk|bang me", 76, 30, 0, GR_sex),
  GFRULE("suck|sucking|blowjob|blow job|give head|oral|swallow|deepthroat|deep throat|mouth|lips|tongue", 72, 26, 0, GR_oral),
  GFRULE("lick|licking|taste|tasting|bite|biting|nibble|nibbling|eat you out|go down on", 70, 24, 0, GR_lick),
  GFRULE("ride|riding|on top|straddle|straddling|grind|grinding|thrust|thrusting|bounce", 70, 24, 0, GR_ride),
  GFRULE("talk dirty|dirty talk|say something dirty|say something naughty|something filthy|naughty|dirty|filthy|nasty|wicked|bad girl|bad boy", 68, 16, 0, GR_dirty),
  GFRULE("tits|boobs|breasts|breast|nipples|nipple|cleavage|chest", 66, 20, 0, GR_breasts),
  GFRULE("ass|butt|bum|arse|booty|spank|spanking|spanked|cheeks", 66, 20, 0, GR_ass),
  GFRULE("horny|turned on|aroused|worked up|hot for you|so hard|need you now", 70, 20, 0, GR_horny),
  GFRULE("tie me|tie you|tied up|handcuff~|rope|blindfold|blindfolded|dominate|dominant|submissive|obey|punish~|master|mistress|collar|leash|restrain~|bondage|bdsm|kink|kinky|fetish|in charge|own me|owned", 66, 18, 0, GR_kink),
  GFRULE("beg|begging|i beg|pretty please|make me", 62, 14, 0, GR_beg),
  GFRULE("more|again|keep going|do not stop|continue|go on|harder|faster|deeper|keep talking", 66, 18, 0, GR_more),
  // --- body, clothes, touch ---
  GFRULE("naked|nude|strip|undress|clothes off|take it off|topless|bare|lingerie|underwear|panties|boxers|bra|thong|nothing on", 62, 16, 0, GR_naked),
  GFRULE("what are you wearing|what do you wear|what you got on|what are you in|what have you got on", 66, 14, 0, GR_wearing),
  GFRULE("i am wearing|i am in my|i have on|i am just in|i am only in", 60, 14, GF_F_MEM, GR_iwear),
  GFRULE("what do you look like|describe yourself|show me|picture of you|pic|pics|selfie|photo|send nudes|are you hot|are you pretty", 56, 10, 0, GR_look),
  GFRULE("kiss|kisses|kissing|make out|snog", 60, 12, 0, GR_kiss),
  GFRULE("touch|touching|hands|fingers|caress|stroke|stroking|rub|rubbing|massage", 58, 12, 0, GR_touch),
  GFRULE("thighs|legs|neck|hips|waist|skin|curves|stomach|belly|shoulders|inner thigh|collarbone|wrist|wrists|lower back|spine|nape|feet|toes|ankle|ankles|belly button|small of my back|knees|arms|jawline|fingertips|inner wrist|behind my knee|hipbones", 52, 10, 0, GR_body),
  GFRULE("bed|bedroom|sheets|covers|pillow|couch|sofa", 48, 10, 0, GR_bed),
  GFRULE("tease|teasing|slow down|slowly|patient|patience|wait for it|take your time", 54, 10, 0, GR_tease),
  GFRULE("shower|bath|bathtub|soap|steam|towel|pool|swim|swimming", 56, 10, 0, GR_shower),
  GFRULE("fantasy|fantasies|dream about|dreamed|imagine|imagining|secret|desire", 54, 12, 0, GR_fantasy),
  // --- feelings and relationship ---
  GFRULE("i want you|want you|i need you|need you|crave you|i want to see you", 65, 16, 0, GR_wantyou),
  GFRULE("miss you|missed you", 55, 8, 0, GR_miss),
  GFRULE("love you|i love|luv you", 55, 8, 0, GR_love),
  GFRULE("you are sexy|you are hot|you are beautiful|you are gorgeous|you are cute|you are pretty|you look good|you look great|so hot|so sexy|you turn me on|you drive me", 62, 10, 0, GR_compl),
  GFRULE("other girls|other women|someone else|another girl|jealous", 54, 8, 0, GR_jealous),
  GFRULE("cuddle|cuddling|hug|hugs|snuggle~|hold me|hold you|romantic|romance", 52, 6, 0, GR_cuddle),
  GFRULE("baby|babe|honey|darling|sweetheart|sexy|beautiful|gorgeous", 50, 6, 0, GR_baby),
  GFRULE("lonely|alone|by myself|on my own", 52, 6, 0, GR_lonely),
  GFRULE("when can i see you|meet me|come over|come here|come to me|see you|visit me|meet up", 58, 10, 0, GR_meet),
  // --- statements about the person (echo) ---
  GFRULE("i want|i would like|i wish", 42, 8, GF_F_MEM, GR_iwant),
  GFRULE("i need", 41, 8, GF_F_MEM, GR_ineed),
  GFRULE("i feel|i am feeling|feeling", 44, 8, GF_F_MEM, GR_ifeel),
  GFRULE("i am", 36, 6, GF_F_MEM, GR_iam),
  GFRULE("my", 20, 4, GF_F_MEM, GR_my),
  // --- everyday topics ---
  GFRULE("hello|hi|hey|hiya|heya|gday|howdy|yo|sup|morning|evening|afternoon", 40, 2, GF_F_GREET, GR_greet),
  GFRULE("how are you|how are you doing|how you going|how are things|how is it going|what are you up to|what is up|what are you doing|wyd", 38, 2, 0, GR_howare),
  GFRULE("your name|who are you|what are you called|what should i call you", 45, 2, 0, GR_name),
  GFRULE("are you real|are you a bot|are you human|are you ai|are you a robot|are you a girl|real person", 45, 2, 0, GR_real),
  GFRULE("good night|goodnight|night night|nighty night|going to bed|off to bed|heading to bed|time for bed|got to sleep", 50, 4, 0, GR_night),
  GFRULE("sleepy|asleep", 40, 4, 0, GR_sleepy),
  GFRULE("bored|boring|nothing to do|nothing on", 48, 6, 0, GR_bored),
  GFRULE("hot|warm|sweating|heat|fever", 44, 6, 0, GR_hot),
  GFRULE("tonight|friday|saturday|weekend|later|tomorrow", 44, 6, 0, GR_tonight),
  GFRULE("work|job|office|tired|exhausted|long day|stressed|busy", 40, 4, 0, GR_work),
  GFRULE("wine|beer|drink|drinks|drunk|champagne|dinner|date|candle~|coffee|cocktail~|whisky|whiskey|chocolate|strawberr~|cream", 40, 4, 0, GR_drinks),
  GFRULE("thank you|thanks|cheers", 35, 2, 0, GR_thanks),
  GFRULE("sorry|apologise|my bad|forgive me", 35, 3, 0, GR_sorry),
  GFRULE("joke|make me laugh|tell me a joke|funny joke", 30, 2, 0, GR_joke),
  GFRULE("yes|yeah|yep|yup|sure|ok|okay|mhm|uh huh|definitely|absolutely", 12, 4, 0, GR_yes),
  GFRULE("no|nope|nah|never|not really", 12, 0, 0, GR_no),
  GFRULE("why|how come|what|how|who|where|when|which", 4, 2, 0, GR_wh),
  GFRULE("you", 6, 2, 0, GR_you),
  GFRULE("doggy|from behind|behind me|bent over|bend over|on all fours|all fours|on my knees|on your knees|face down", 71, 26, 0, GR_doggy),
  GFRULE("missionary|on my back|spread|legs open|open your legs|wrap my legs|legs around|between your legs|between my legs|lie back|lay back", 70, 24, 0, GR_missionary),
  GFRULE("finger~|touch yourself|touching yourself|play with yourself|playing with yourself|masturbat~|jerk off|jerking|jerk it|stroke it|stroking it|rub yourself|pleasure yourself|self pleasure", 78, 28, 0, GR_selftouch),
  GFRULE("moan~|scream~|gasp~|whimper~|breathless|panting|pant|groan~|sigh~|purr~|shiver~|tremble~|trembling|shaking|shake", 64, 20, 0, GR_moan),
  GFRULE("sweat~|skin on skin|body heat|steamy|slick|sticky|flushed|blush~|blushing|glowing", 52, 12, 0, GR_sweat),
  GFRULE("oil|oiled|lotion|lube|slippery|massage oil|baby oil|moisturiser|sunscreen|suntan", 58, 14, 0, GR_oil),
  GFRULE("toy~|vibrator|dildo|plug|wand|buzz~|vibrat~|bullet|strap on|strapon", 72, 26, 0, GR_toys),
  GFRULE("hickey|love bite|love bites|marks|scratch~|claw~|bruise~|nails|fingernails|bite marks", 62, 16, 0, GR_marks),
  GFRULE("pull my hair|hair|grab~|pin me|pinned|pin you|against the wall|wall|push me|push you|shove", 56, 14, 0, GR_grab),
  GFRULE("whisper~|ear|ears|earlobe|breath|breathe|breathing|hot breath|murmur~|neck kiss", 56, 14, 0, GR_whisper),
  GFRULE("striptease|strip tease|dance|dancing|lap dance|twirl|twerk~|slow dance|sway~|grind on", 62, 18, 0, GR_dance),
  GFRULE("stockings|heels|high heels|garter~|corset|lace|silk|satin|robe|negligee|fishnet~|stilettos|leather|latex|skirt|dress|jeans|shirt|tshirt|hoodie|socks|pyjamas|pajamas|nightie", 54, 12, 0, GR_lingerie),
  GFRULE("costume|roleplay|role play|pretend|cosplay|secretary|nurse|maid|cop|officer|bartender|stripper|massage therapist|doctor|plumber|pizza guy|delivery|uniform", 62, 16, 0, GR_roleplay),
  GFRULE("wake up|morning sex|waking up|wake me|woke up|just woke|alarm|snooze|sunrise", 52, 10, 0, GR_wakeup),
  GFRULE("afterglow|after sex|aftercare|catch my breath|catching my breath|worn out|spent|satisfied|that was amazing|round two|second round|again later|next round", 60, 6, 0, GR_afterglow),
  GFRULE("all night|marathon|stamina|last long|endurance|hours|round the clock|nonstop|non stop|insatiable|never get enough|cannot get enough", 60, 14, 0, GR_stamina),
  GFRULE("bigger|huge|thick|tight|tighter|size|inches|massive|girth", 58, 18, 0, GR_size),
  GFRULE("pervert|perv|dirty mind|filthy mind|horndog|you are bad|you are naughty|you are dirty|you are filthy|you are evil|you are a tease|you are trouble|you are wicked|you are dangerous", 63, 12, 0, GR_pervert),
  GFRULE("sext~|nudes|dirty pics|nude pics|naughty pics|send me something|send me a pic|show me yours|picture of yourself", 64, 16, 0, GR_sext),
  GFRULE("truth or dare|dare me|truth|dare|would you rather|never have i ever|strip poker|play a game|game|games|spin the bottle|twenty questions|20 questions", 56, 12, 0, GR_games),
  GFRULE("thinking of you|thinking about you|thought of you|thought about you|think about you|think of you|dreaming of you|dreamt of you|dreamed of you|cannot stop thinking|you cross my mind|on my mind", 60, 12, 0, GR_thinking),
  GFRULE("good boy|good girl|well done|well behaved|obedient|behave|behaving|behaved|be good|i was good|i have been good", 58, 14, 0, GR_goodgirl),
  GFRULE("sir|maam|goddess|queen|king|princess|your majesty|my lady|yes sir|yes maam", 58, 12, 0, GR_sir),
  GFRULE("rough|roughly|pound~|slam~|take me hard|wild|animal|primal|raw|passionate|passion|intense|intensity|devour|ravish~|savage|hungry for you|ravage", 64, 20, 0, GR_rough),
  GFRULE("gentle|gently|softly|tender|tenderly|sensual|sensually|candlelight|rose petals|massage me", 54, 8, 0, GR_gentle),
  GFRULE("ice|ice cube|ice cubes|whipped cream|honey|feather|candle wax|wax|silk scarf|scarf|ribbon|chocolate sauce|syrup", 56, 14, 0, GR_sensory),
  GFRULE("kitchen|counter|table|desk|car|backseat|back seat|balcony|beach|hotel|elevator|lift|garden|roof|rooftop|outdoors|outside|hot tub|jacuzzi|bathroom|floor|stairs|hallway|park|forest|sand|library|toilet|stairwell", 52, 14, 0, GR_places),
  GFRULE("birthday|anniversary|valentine~|christmas|new year|party|wedding|honeymoon|holiday|holidays|vacation|weekend away|getaway", 40, 6, 0, GR_occasion),
  GFRULE("rain|raining|storm|thunder|lightning|cold|freezing|winter|chilly|summer|sunny|snow|snowing|windy|humid|heatwave|sunburn", 38, 6, 0, GR_weather),
  GFRULE("hungry|breakfast|lunch|food|cook~|cooking|eat|eating|dessert|pizza|snack|bacon|burger|pasta|cake|ice cream|sandwich|tea|toast", 38, 4, 0, GR_food),
  GFRULE("music|song|songs|playlist|sing|singing|album|band|spotify|guitar|piano|concert", 36, 4, 0, GR_music),
  GFRULE("movie|movies|film|netflix|tv|series|binge|cinema", 36, 4, 0, GR_movies),
  GFRULE("gym|workout|exercise|running|muscle~|abs|biceps|yoga|stretch~|training|sweaty|fit|toned|six pack|pushups|squats|flexible|flexibility", 42, 10, 0, GR_gym),
  GFRULE("sad|upset|crying|cried|depressed|anxious|nervous|scared|afraid|heartbroken|worried|bad day|rough day|miserable|gloomy|devastated|overwhelmed", 50, 0, 0, GR_sad),
  GFRULE("happy|excited|great|amazing|awesome|fantastic|good news|brilliant|wonderful|celebrate|celebrating|won|promoted|passed|got the job|best day", 44, 4, 0, GR_happy),
  GFRULE("bye|goodbye|see you later|see you soon|cya|gtg|got to go|have to go|leaving|brb|talk later|talk soon|catch you later|off now|signing off", 52, 2, 0, GR_bye),
  GFRULE("you are", 34, 4, 0, GR_youare),
  GFRULE("do you|would you|can you|will you|could you|are you|have you|did you|were you|do you ever", 32, 4, 0, GR_doyou),
  GFRULE("i like|i enjoy|i adore|i love to|i love it when", 38, 8, 0, GR_ilike),
  GFRULE("i do not like|i hate|i dislike|i do not want|i do not|i am not", 37, 2, 0, GR_idont),
  GFRULE("i have|i have got|i got|i own", 34, 4, 0, GR_ihave),
  GFRULE("i can|i could|i will|i would|i might|i should|i must", 33, 5, 0, GR_iwill),
  GFRULE("i was|i used to|i did|i went|i had|i have been|i have just", 32, 4, 0, GR_iwas),
  GFRULE("because|cause|since|reason|that is why|the reason", 30, 2, 0, GR_because),
  GFRULE("everyone|everybody|always|never|nobody|no one|everything|nothing|all men|all women|all guys|all girls", 28, 2, 0, GR_always),
  GFRULE("idk|dunno|no idea|not sure|nevermind|never mind|forget it|whatever|hmm|hm|um|uh|erm|meh|maybe|perhaps|i guess|i suppose", 18, 1, 0, GR_deflect),
  GFRULE("lol|haha~|hehe~|lmao|rofl|giggle~|laugh~|hilarious|smile|smiling|grin|grinning|wink|winking|cheeky", 28, 4, 0, GR_laugh),
  GFRULE("wow|omg|oh my god|oh my|holy~|damn|jeez|whoa|ooh|mmm|mm|ahh|yum|delicious|tasty|nice|lovely", 24, 4, 0, GR_wow),
  GFRULE("xoxo|muah|mwah|hearts|love hearts|heart eyes|smooch~|mwuah|blow kiss", 56, 6, 0, GR_xoxo),
  GFRULE("seduce me|woo me|turn me on|excite me|impress me|entertain me|convince me|make me blush|make me melt|spoil me|take me|have me|claim me|use me|pleasure me|please me|satisfy me|worship me|tempt me|surprise me|show me what you got|do something|make me feel", 72, 24, 0, GR_seduce),
  GFRULE("what would you do|what will you do|what are you going to do|what do you want to do|what do you want|what do you like|what turns you on|tell me what you would do|tell me what you want|tell me a story|bedtime story|story|narrate|describe it|your fantasy|your fantasies|what do you fantasise about|what do you fantasize about|what are you thinking|what are you thinking about|what is on your mind", 68, 18, 0, GR_whatdo),
  GFRULE("ache|aching|yearn~|longing|burning|craving|crave|desperate|desperately|cannot wait|cannot stand it|going crazy|driving me crazy|driving me wild|losing control|losing my mind|need release|need it|so bad|badly", 66, 20, 0, GR_ache),
  GFRULE("stay the night|stay over|sleepover|spend the night|sleep over|stay with me|my place|your place|invite you|invite me|come round|come around|pop over|drop by|drop in", 60, 10, 0, GR_stay),
  GFRULE("on my way|omw|heading over|coming over|at your door|nearly there|almost there|be there soon|be there in|just arrived|i am here|i am outside|outside your door|pulling up", 62, 14, 0, GR_omw),
  GFRULE("you are mine|i am yours|belong to you|belong to me|yours|mine|own you|claim you|possess~|property|all yours|all mine|you belong|mark you|keep you", 60, 14, 0, GR_possess),
  GFRULE("watch me|watch you|watching you|watching me|mirror|camera|video|webcam|cam|film us|record us|record~|peek|peeking|spy|spying|voyeur", 58, 14, 0, GR_watch),
  GFRULE("hurry|hurry up|come on|impatient|quickly|right now|immediately|asap|do it now|do not make me wait|waiting", 56, 12, 0, GR_hurry),
  GFRULE("i like you|i like your|i love your|you smell|you taste|you feel|your voice|your eyes|your smile|your lips|your body|your mind|your laugh|your hair|your hands|your touch|your kiss|your mouth", 60, 10, 0, GR_compl2),
  GFRULE("confess~|confession|admit|honest|honestly|truthfully|secrets|can i tell you|i have to tell you|i need to tell you|promise not to laugh|do not tell|do not judge", 56, 10, 0, GR_confess),
  GFRULE("you tease|such a tease|stop teasing|quit teasing|do not tease|stop playing", 62, 12, 0, GR_tease2),
  GFRULE("how old are you|what is your age|your age|how old", 45, 2, 0, GR_age),
  GFRULE("where are you from|where do you live|where are you|do you live|your home|your house|your room", 47, 4, 0, GR_about),
  GFRULE("single|boyfriend|husband|relationship|girlfriend|are you taken|dating|date me|go out with me|be my girlfriend|be mine|marry me|my girlfriend", 56, 6, 0, GR_girlfriend),
};
#define GF_NRULES ((int)(sizeof(GF_RULES) / sizeof(GF_RULES[0])))
