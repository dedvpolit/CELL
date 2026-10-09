#pragma once
#include <algorithm>
#include <array>
#include <random>
#include <string>
#include <vector>
#include <glm/glm.hpp>

// Diaries: one per pocket. The story is told in read order: the N-th diary the player opens shows
// step N, whichever pocket it lies in. Each step has 2-3 variants with the same key facts; one is
// picked per map seed. ~WORD~ is drawn as an ink stain.
namespace Diaries {

constexpr int kStepCount = 12;
constexpr int kMaxVariants = 3;

struct StoryStep {
    int variantCount;
    const char* variants[kMaxVariants];
};

constexpr StoryStep kStory[kStepCount] = {
    // 1
    { 3, {
        "Day 1. Mom says writing helps. Helps with what, she didn't say. So, hi, diary. M. forgot his lighter at my place again. He swears he's quitting by summer. Third summer in a row. On Saturday we're finally going to the old quarry. He says I'll chicken out. I won't. Mom and Dad were quiet at dinner. Quiet is worse than loud. Anyway. I'm fine.",
        "Day 1. This was Mom's idea. She bought the notebook and everything, so now I have to. M. left his lighter on my desk again. He'll want it back on Saturday - we're going to the quarry, we've been planning it forever. He still hasn't texted the girl from the other class. Three months of \"tomorrow\". Dinner was quiet again. I'm fine.",
        "Day 1. Mom thinks I don't talk enough, so now I write. Fine. M. forgot his lighter here. Again. He says he'll quit by summer, I say he said that last summer, he throws a pillow at me. Saturday is the quarry. Finally. He bets I'll chicken out at the last minute. Loser buys the chips. I'm not losing. Mom and Dad aren't really talking. I'm fine.",
    } },
    // 2
    { 3, {
        "Day 5. Friday. Dad didn't come home for dinner. He came home at midnight and then it got loud. Really loud. Alarm set for 6:00, we leave early. Everything else on silent. I don't want to hear anything tonight. Not them, not anyone. Phone face down, pillow over my head. M. texted before that: \"don't forget my lighter\". It's in my backpack. Saturday tomorrow.",
        "Day 5. Got a C in physics, whatever. M. says physics is fake anyway. He texted: \"bring my lighter, we leave early, don't oversleep\". As if. Tonight they're shouting again. Something about money, something about \"your son\". So: alarm at 6:00, phone on silent, face down. Pillow over my head until they're just noise. Tomorrow is the quarry. That's all I'm thinking about.",
        "Day 5. Dad's car came back late. Now the kitchen door is closed and they're yelling through it. I don't want to hear any of it. Not tonight. I set the alarm for 6:00 and put everything else on silent. Pillow over my head. M. wrote earlier: \"lighter. early. don't be late, chicken.\" I packed it already. I'll sleep and then it'll be Saturday.",
    } },
    // 3
    { 3, {
        "Day 8. Two missed calls from M. Saturday, 01:14 and 01:20. I saw them in the morning. I don't want to write about the ~weekend~. School today. Nobody sat at his desk. Not even the kids who always fight over the window seats. The teachers were nice to me. ALL of them. Even the physics one.",
        "Day 8. My phone still shows them. M., 01:14. M., 01:20. Missed. I didn't hear. I'm not writing about ~Saturday~. I'm not. School was weird. His desk was empty all day and everyone walked around it. Ms. K. asked if I wanted to go home early. She NEVER lets anyone go home early.",
        "Day 8. 01:14. 01:20. Two calls. I was asleep. I found them in the morning and called back and called back. I don't want to talk about the ~weekend~. At school nobody sat next to me. Nobody sat at his desk either. Everyone was SO nice. I hate it.",
    } },
    // 4
    { 3, {
        "Day 10. Mom ironed a black shirt for the ~funeral~. I didn't have a black shirt. Now I do. The whole class signed a card for his mom. I wrote my name and then I couldn't think of anything else. From the bus you can see they put a fence around the quarry. New one. Ugly. His lighter doesn't work anymore. Just sparks. I'll buy him a new one. I'm FINE.",
        "Day 10. The black shirt is scratchy. Mom bought it for ~Thursday~. Everyone signed the card for his mom. I had it last and the pen died. Or I let it die. There's a fence around the quarry now, I saw it from the bus window. I tried his lighter. Click. Sparks. Nothing. I'll get him a new one, the blue kind he likes. I'm FINE. Mom, I'm FINE.",
        "Day 10. Things that are new: a black shirt. A card with 27 names for his mom - mine is the smallest one, I wrote ~sorry~. A fence around the quarry, I saw it from the bus and looked at my shoes until my stop. Things that are old: his lighter. It only sparks. I'll buy him a new one. He'll pretend he doesn't care. I'm FINE.",
    } },
    // 5
    { 2, {
        "Day 13. Didn't go to school. Mom thinks I did. I sent M. a meme about a chicken. Under his name it says \"last seen 7 days ago\". I sent another one. He's going to have SO many to scroll through. Dad said at breakfast I should be over ~it~ by now. It's been a week. A WEEK. Clicked his lighter 9 times. Sparks. Nothing. Tomorrow I'll buy gas for it.",
        "Day 13. Things I did today: didn't go to school. Sent M. three memes. It still says \"last seen a week ago\". He's never been offline this long, not even when his phone fell in the soup. Dad says I need to move on from ~it~. Like it's a bus stop. Clicked the lighter 12 times. 12 sparks. 0 fire. I'm keeping COUNT. Someone has to.",
    } },
    // 6
    { 3, {
        "Day 16. I scratched an M on his lighter. With the compass from my geometry set. So it remembers whose it is. It still won't light. At night there's something in the corner of my room. Tall. Wrapped up like a person under a sheet. It doesn't move when I look. I put the blanket over my head and stayed there till morning. Someone walked down the hall at 3. Everyone was ASLEEP.",
        "Day 16. Couldn't sleep again. I took the compass out of my pencil case and scratched an M into the lighter. Deep. So it knows. Click. Spark. Nothing. Around 2 I saw IT. In the corner, by the wardrobe. Something wrapped in cloth, standing very still, like it was waiting for me to ~notice~. I pulled the blanket over my head. Under the blanket it's only me. Footsteps in the hallway. Not Mom's.",
        "Day 16. There's an M on the lighter now. I scratched it with the compass during math. Ms. K. saw and said nothing. Everyone says nothing now. Night: the corner by the door is darker than the others. Something is standing in it, wrapped up, head to feet. I don't know what IT is. I know it's waiting. Blanket over my head. Slow footsteps in the hall. Mom and Dad were asleep. I ~checked~.",
    } },
    // 7
    { 2, {
        "Day 19. Dad left. He took the big suitcase and the photos from the hallway. He said it's for work. Work doesn't need the photos. Mom works two shifts now. There are notes on the fridge: \"soup in the pot\", \"love you\", \"eat SOMETHING\". The hallway in our flat is longer. I walked it twice to be sure. 23 clicks today.",
        "Day 19. The big suitcase is gone. So is Dad. He hugged me at the door and said \"be a man about it\". About WHAT, Dad. Mom came home at 11 and left again at 6. Her notes are everywhere. The hallway takes longer to walk now. 14 steps, then 17, then 19. I counted. The lighter: 27 clicks. The M is getting deeper.",
    } },
    // 8
    { 3, {
        "Day 19. Or 20? It said 19 yesterday. ~M~, you'd laugh at me. You'd say I'm being dramatic. Maybe. The night light SHAKES when I don't look at it. Someone is standing at the end of the hallway. I KNOW THE SHAPE. I don't know from where. 33 clicks. Still nothing.",
        "Day 19. Day 19 again. My phone says 20. My phone is LYING. ~M~, remember when we stayed up till 4 and you said the dark doesn't do anything, it's just dark? It does things now. The night light trembles. Someone stands at the end of the hall and doesn't come closer. 36 clicks. YOU'D KNOW WHAT TO DO.",
        "Day 19? I wrote Day 19 yesterday. And the day before. ~M~, I keep the lighter in my pocket. The M is so deep now I can feel it with my thumb. 38 clicks. Last night the night light shook like someone was blowing on it. There's a SHAPE at the end of the hallway. Wrapped. It's patient. It's SO PATIENT.",
    } },
    // 9
    { 2, {
        "Day 19? He called twice. 01:14. 01:20. I was asleep. My phone was on SILENT. He called TWICE. What do you call someone for at 01:14? What was he going to say? I SHOULD HAVE ~ANSWERED~. I SHOULD HAVE. IT is closer tonight. It breathes like someone under a blanket. 39 clicks. The M is deeper. It doesn't help.",
        "Day 19. Still 19. I keep doing the math. 01:14. 01:20. Six minutes. He waited six minutes and called AGAIN. My phone was on SILENT and I was ASLEEP. I SHOULD HAVE ~ANSWERED~. ~M~, I SHOULD HAVE. IT stands by my bed now. I can hear it breathing. Slow. Under cloth. The M is so deep the lighter is sharp there.",
    } },
    // 10
    { 3, {
        "Day ? 41. 44. 47. CLICK CLICK CLICK. NOTHING. THE M IS DEEPER. Mom's note: \"please eat something, please\". I read it eleven times. I ate the soup. I THINK. IT STOOD AT MY DOOR ALL NIGHT. IT DIDN'T COME IN. IT DOESN'T NEED TO. ~M~ ~M~ ~M~",
        "DAY ? I DON'T KNOW. 41 44 47 50. SPARKS. NO FIRE. NO FIRE EVER. the hallway is ~forty~ steps now. Mom left a note: \"I'm here. Knock if you need me.\" I didn't knock. IT knocked. IT KNOCKED ON MY DOOR FROM THE INSIDE.",
        "Day ?? 41. 44. 47. i stopped writing the days, they don't change. ~M~ CALLED TWICE. TWICE. the M on the lighter is a hole now. mom's notes keep coming: \"eat\", \"sleep\", \"I love you\". IT STANDS IN THE DOOR. I PULL THE BLANKET UP. UNDER THE BLANKET IT'S ONLY ME. ONLY ME.",
    } },
    // 11
    { 3, {
        "DAY ??? IT SPOKE TONIGHT. IT SOUNDS LIKE ME. IT SAID THE THINGS I SAY. THE LIGHTER STILL SMELLS LIKE HIS CIGARETTES. I KEEP IT UNDER MY PILLOW. ~CLICK~. I'M FINE I'M FINE I'M FINE I'M",
        "DAY ??? I HEARD IT IN THE HALL AND IT HAD MY VOICE. IT WAS COUNTING CLICKS. MY NUMBERS. HIS LIGHTER STILL SMELLS LIKE HIS CIGARETTES. I KEEP IT IN A SOCK SO THE SMELL ~STAYS~. I'M FINE. I'M FINE I'M FINE I'M FI",
        "DAY ??? DAY ??? DAY. IT IS IN THE ROOM. IT IS WRAPPED IN SOMETHING. IT BREATHES WHEN I BREATHE. IT SOUNDS LIKE ME WHEN I SAY ~HIS~ NAME. the lighter smells like his cigarettes. that's the only quiet thing. I'M FINE I'M FINE I'M",
    } },
    // 12
    { 2, {
        "Day 31. I slept. I saw it today. Up close. It was wrapped in my blanket. It had my face. It wasn't chasing me. It was just tired. I didn't click the lighter today. The M is still there. M., I'm keeping it. I think I'm going to tell Mom. Not everything. Just the first word. The door is right there.",
        "Day 31. A real day 31, I checked the calendar on the fridge, under Mom's notes. Last night I finally looked at it. It was me. Wrapped in my blanket, sitting on the floor. Not scary. Just tired. I didn't click the lighter today. The M is still there. Mom is home tonight. I'm going to knock. Not everything. Just the first word. The door is right there.",
    } },
};

struct PlacedDiary {
    int pocketCellX = 0;
    int pocketCellZ = 0;
    int storyStep = -1;  // assigned when first read
    std::string text;    // empty until read
};

inline std::vector<PlacedDiary> PlaceInPockets(const std::vector<glm::ivec2>& pocketCenters) {
    std::vector<PlacedDiary> result(pocketCenters.size());
    for (size_t i = 0; i < pocketCenters.size(); ++i) {
        result[i].pocketCellX = pocketCenters[i].x;
        result[i].pocketCellZ = pocketCenters[i].y;
    }
    return result;
}

template <typename Rng>
std::array<int, kStepCount> PickVariants(Rng& rng) {
    std::array<int, kStepCount> variants{};
    for (int s = 0; s < kStepCount; ++s)
        variants[s] = std::uniform_int_distribution<int>(0, kStory[s].variantCount - 1)(rng);
    return variants;
}

// Steps past the end repeat the last one; maps have at most kStepCount pockets.
inline const char* StepText(int step, const std::array<int, kStepCount>& variants) {
    step = std::clamp(step, 0, kStepCount - 1);
    return kStory[step].variants[variants[step]];
}

} // namespace Diaries
