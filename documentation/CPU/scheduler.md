# Scheduler

There are some core design philosophies for the WallOS scheduler, that are largely platform independent.

For the purposes of this, `CPU` refers to a logical compute unit (threads on x86, cores on aarch64, etc.).

## Philosophies

- Each CPU manages it's own runqueue, referred to as `cpu_rq` for the purposes of this document.
-  
