# Wielding: weapons and weight

In VR a weapon is only as good as the way it sits in your hand. The first headset session
with Daddy Bug Smasher showed the problem. Its hammer was held exactly as it was grabbed,
so a hammer taken at an odd angle *stayed* at that angle. The player twisted their wrist,
tried the other hand, let go to re-grip, and dropped the hammer nine times. Blade & Sorcery
gets this right, and `vrui_wield` (vrui.h section 14, `vrui/src/vrui_wield.c`) copies what it
does.

The toolbox has two stations for it, past the left end of the row:
- **Wielding:** a rack with a sword, Daddy's hammer, a spear and a dagger, and a sandbag to
  hit.
- **Weights:** a feather, a ball, a brick, a kettlebell and an anvil, with a lane to throw
  them down.

Daddy Bug Smasher's hammer is wielded the same way.

```c
VruiWieldSpec s = vrui_wield_spec(VRUI_WEIGHT_HEAVY);        // tested defaults for its weight
s.grip[0] = (VruiGrip){ { 0, 0.04f, 0 }, { 0, 0.78f, 0 },    // the handle: a segment in its own frame
                        { 0, 0, 1 }, 1, false };             // its face (+Z) toward the knuckles, one way round
s.ngrips = 1;
s.center = (Vector3){ 0, 0.8f, 0 };                          // head-heavy
VruiWield w = vrui_wield(id, &hammer_pose, &s);              // every frame; draw it at hammer_pose
```

## What players expect, and how it's done

| Players expect | How |
|---|---|
| to pick it up by the handle and have it **sit right**, however it was lying | Each handle is a segment in the thing's frame, with a `face` (a hammer's face, a blade's edge). On grab, the thing turns so the handle runs out of the thumb side of the fist (the grip pose's -Z) and its face points where the knuckles do (-Y). Where along the handle is where the hand took it. |
| it to turn as little as possible getting there | `rolls` says how many ways round it can sit: 1 (a hammer: face forward), 2 (a sword: either edge), 0 (a round staff: any). The nearest one to how it lies is chosen. A `reversible` handle (a dagger) is taken upside down if it's lying that way: an icepick grip. |
| a second hand to **steer** it | With `two_handed`, a second hand on a handle holds it too. The main hand anchors it, the line from it through the second hand aims it, and the main hand sets the roll. Without it, the other hand takes it over (swap hands). |
| to **choke up** or slide down a shaft | `sticky` + `slide`: a sticky grip holds until the hand is fully open, so loosening it (below the grab button's release point) slides the hand along the handle instead of dropping it. Moving the hand along the handle moves the hand, not the thing. With one loose hand, a tilted shaft slides down through it. |
| heavy to feel **heavy** | The thing follows the hands through springs. Position: 14/√mass Hz. Rotation: 3/√I Hz, where I = mass × (lever² + 0.02) and the lever runs from the hand to the balance point. So a hammer held at the end of its shaft swings behind the wrist; choked up, it's quick. Two hands are 2–2.5× stiffer. The lag is felt as a hum. |
| an anvil to need **two hands** | `lift_hands = 2`: with fewer hands it won't rise above where it lay (it drags), and the hand hums with strain. |
| to throw light things far and heavy things not at all | A release keeps the thing's own motion (its lag is part of that), capped at `max_throw`. |
| dropped things to **fall** and settle | Loose, it falls (`gravity`, `drag`), bounces (`bounce`) off `spec.ground`, rubs to a stop and tips onto its flattest side. A game with its own physics sets `own_physics` and moves it itself. |
| to get a dropped weapon back | `pull`: point the laser at it and squeeze the grip; it flies into the hand in 0.3 s, by its handle. |

## Weights

`vrui_wield_spec(weight)` gives a starting point for each of five weights. Take one as it
is, or change a number or two:

| Weight | Like | Mass | Follows | Throw | Else |
|---|---|---|---|---|---|
| FEATHER | a feather, a leaf | 0.02 kg | exactly (on your hand) | 4 m/s | drag 4: drifts down |
| LIGHT | a ball, a dagger | 0.3 kg | 25 Hz: all but exactly | 14 m/s | bouncy |
| MEDIUM | a sword, a brick | 1.5 kg | 11 Hz: a moment behind | 10 m/s | |
| HEAVY | a hammer, a kettlebell | 4 kg | 7 Hz; slower still held far from its balance | 6 m/s | sticky, slides |
| HUGE | an anvil | 40 kg | 3 Hz | 1.5 m/s | two hands to lift |

The springs are a little underdamped (0.75), so heavy things swing through a little past
where you stop. That's the weight you feel: the controllers can't push back, so the eyes
have to carry it.

## The Wielding station

Four weapons on a rack, each a different case:

| | Handle | Why it's there |
|---|---|---|
| **Sword** (1.5 kg) | 22 cm, two ways round | Picked up any way, it settles blade-out, edge to the knuckles. The long grip takes two hands. |
| **Hammer** (3 kg, 0.95 m) | 74 cm, face one way | Head-heavy: at the end of the shaft it swings behind you. Loosen your grip and slide up to choke up, or add your other hand. |
| **Spear** (2 kg, 1.9 m) | the whole shaft, any way round | Slide anywhere; two hands to aim. |
| **Dagger** (0.3 kg) | 9 cm, reversible | Picked up point-down, it's held point-down. |

The **sandbag** swings with a blow. The part that hits is the weapon's blade or head, not
your hand, at its own speed (its motion plus its spin). The thump comes from where you hit,
and you feel it. The panel turns off stickiness, sliding and weight one at a time, and shows
the handles, so you can feel what each one adds.

## Physics guns

Beside the Weights table (`station_guns.c`) stand two tools for moving things from afar,
so you can feel what weight does when your hand isn't on the thing. Pick one up with the
grip: it snaps into your hand pointing where your laser would, and goes back to its stand
when you let go. While you hold one, that hand has no laser and its buttons are the gun's
(`vrui_claim_input`), so reeling with the stick doesn't also turn you.

| Gun | Controls | Behavior |
|---|---|---|
| **Physgun** (Garry's Mod) | hold the trigger on a thing; stick forward/back reels it out/in; twist your wrist to turn it; A freezes it | the thing hangs where the beam caught it and turns with the gun. Heavy things lag (a feather at once, an anvil a beat behind), and the beam bends to show it. Let go and it flies on with the swing it had. Frozen, it stays in the air in a blue cage until the beam or a hand takes it |
| **Gravity gun** (Half-Life 2) | A lifts the thing you aim at (A again drops it); the trigger punts | the thing floats just in front of the gun. A punt is the same push for everything (12 N·s, capped at 20 m/s), so a ball flies down the lane and a kettlebell barely moves. It can't lift anything over 30 kg: the anvil only gets a tug |

The guns move the Weights things with one call:

```c
// every frame the beam holds it: take it off physics, and set its pose yourself
vrui_wield_set_motion(id, false, Vector3Zero(), Vector3Zero());
thing_pose = where_the_beam_wants_it;
// let go: it falls, bounces and settles by itself, with the motion it had
vrui_wield_set_motion(id, true, w.velocity, w.angular_velocity);
```

A punt is the same call with the punt's velocity. A throw from a gun counts on the
Weights panel like a throw from a hand.

Tests: `tests/toolbox/guns.sfxt`:
- the physgun lifts the brick and freezes it in the air (break switch
  `toolbox_physgun_no_freeze`)
- the gravity gun lifts the ball and punts it more than 4 m down the lane
- it can't lift the anvil (`toolbox_gravgun_lifts_anything`)

## Tests

`make test T=wield`:

| Case | Proves | Break switch |
|---|---|---|
| sword-settles-into-the-hand | a sword lying sideways ends up blade-out, edge to the knuckles, held at its handle | `vrui_wield_keeps_grab_angle` |
| hammer-face-forward | a hammer lying face-up turns over: face to the knuckles | `vrui_wield_keeps_grab_angle` |
| second-hand-steers | the far end follows the second hand; still held at the first | `vrui_wield_second_hand_ignored` |
| weight-lags | a heavy hammer trails a quick move, then catches up | `vrui_wield_weightless` |
| sticky-loosened-slides | a loosened grip holds and slides; fully open drops | `vrui_wield_not_sticky` |
| anvil-takes-two-hands | one hand drags it, two lift it | |
| throw-flies-and-lands | a thrown ball flies meters and comes to rest on the floor | |
| laser-pulls-it-to-the-hand | laser + grip brings a sword into the hand, by its handle | |

In the toolbox, `tests/toolbox/rack.sfxt` takes the sword off the rack by its handle, and
checks that one hand only drags the anvil.
