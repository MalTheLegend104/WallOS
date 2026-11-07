# Scheduler

This folder contains all documentation on the scheduler (WFES).

## Spec

The spec is the most complicated part of the scheduler, and as a result, I wrote it in latex.
There is a compiled PDF for this as well.

[PDF](scheduler.pdf)

[TEX](scheduler.tex)

## Implementation

The [implementation](implementation.md) doc is a guide that outlines some general implementation and behavior of the WFES core.
This includes how tasks are created and handled, and the general flow of the scheduler.
It's meant to be a guide for someone intending to work on the scheduler or interact with the scheduler, it doesn't cover every minute detail.

## Porting

The [porting](porting.md) doc is outlines what an architecture port needs to provide to the core.
It is meant as an overview, not a guide.
All of the stuff discussed in this doc must be implemented for a port to work, there is no way for a "partial" port.
