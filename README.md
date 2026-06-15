# Spikot

A cat with a brain you can watch think.

Most game AI is a state machine wearing a trench coat, or an RL agent that did all its learning offline and now just replays a frozen policy. Spikot does neither. The cat runs on a spiking neural network written from scratch: leaky integrate-and-fire neurons, synapses that rewire through STDP, learning that happens live while you watch. No pretraining. The cat starts dumb and gets less dumb in front of you.

The visual is procedural pixel art. Every cat is seeded from a genome (body size, ear angle, tail length, fur, colors), so no two look the same. Emotions aren't canned animations, they're driven by neural activity: scared ears drop and pupils blow up, curious ears tilt forward, angry tail arcs.

## Stack

```
C        SNN simulation (LIF neurons + STDP), raw speed
raylib   rendering, OpenGL under the hood, native C
```

No shaders. The look comes from primitives and tiny pixel textures upscaled with point filtering.

## Build

You need a C compiler and CMake. raylib pulls itself in through CMake FetchContent, so there's nothing to install by hand.

```
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build
./build/bin/spikot.exe
```

First configure takes a minute while raylib clones and compiles. After that it's fast.

## Layout

```
src/
  main.c        window, game loop, render, headless test modes
  config.h      all tunables in one place
  snn/          LIF neuron, network, eligibility-trace R-STDP, homeostasis
  cat/          genome + procedural pixel generator, emotion expressions
  env/          grid world, food, obstacles, per-cat vision
  agent/        bridges a brain to a body: encode vision, decode action + voice
```

Headless modes for fast iteration without the window: `--snn-test` (raw spiking), `--agent-test` (foraging + voice over time), `--export` (cat sprites in every emotion), `--shot` (one frame of the live sim to a PNG).

## Why SNN and not regular RL

STDP is local and online. A synapse strengthens or weakens based purely on which of its two neurons fired first, no backprop, no replay buffer, no training phase. Add a reward modulator (food is +1, hitting something is -1) and you get R-STDP, which is about as close to "biologically plausible reinforcement learning" as you can get in a few lines of C. The point isn't benchmark scores. The point is that the learning is legible: you can render every neuron as a dot, watch it spike, and actually see the cat figure things out.

## Two cats

You can drop in a second cat. Both share one grid, see and bump into each other, and each runs its own brain. They also talk: every brain has a small group of voice neurons whose firing becomes a signal the other cat hears on its input neurons. So the channel is real, bidirectional, and plastic, the kind of thing where you sit and wonder whether they're actually coordinating or just chattering.

## Status

The whole loop runs. Procedural cats with moods, a spiking brain that drives the body and learns online, a grid to forage in, two cats chatting through neural signals, and a live panel of every neuron firing.

One honest open problem: the cat doesn't yet get measurably *better* at foraging over time. The plasticity machinery is all there (eligibility traces, reward modulation, synaptic homeostasis) and the brain clearly responds to reward, but stable credit assignment in a recurrent spiking net is hard, and partial observability (the cat only sees a few tiles out) doesn't help. That's the next mountain.
