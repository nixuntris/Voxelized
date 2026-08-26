This is an implementantion of a cpu raytracer.
It's meant to run in real time, and is developed on a ryzen 5950x, it currently doesn't have completely implemented avx2 instructions for simding ray traversal.

IT REQUIRES AVX2 INSTRUCTIONS TO BE SUPPORTED BY YOUR CPU OR OTHERWISE IT WILL CRASH (If you are a judge reading this, your windows VM might not have avx2 enabled)

![](preview.jpg)

It uses raymarching and fallbacks on multi level dda if the step is too small.
I calculate distance fields per chunk, on 4 different levels, 4,8,16,32 (each chunk is 32^3 voxels)
I reproject voxels per every odd frame to the next frame and then rewrite them with a from scratch low res ray acceleration pass.
It uses branchless DDA
I quantize data per distance field
Occupancy masks use a dedupe algorithm to remove data stored
Each chunk is palletized to 8 bit per voxel
I store lighting as R8G8B8 it's reallocated on demand.

The project has raylib linked to where it's installed by the raylib installer, if you have it installed somewhere else you need to change the COMPILER_PATH variable in the Makefile to where it's saved.
I have not tested it on linux, this was developed on windows, so there maybe unexpected issues there.
This is also setup to be plug and play with visual studio code. If you wish to use another IDE/code editor you may need to do some edits.
You're allowed to do with this project as you please, just credit where you got this code from or based it on.

(This setup is based of educ8s Raylib-CPP Starter Template for VS Code)

Controls are:
W - forward
S - backwards
Mouse - rotation