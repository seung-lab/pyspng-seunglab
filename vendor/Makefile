
SPNGDIR := libspng-0.7.2
MINIZDIR := miniz-2.2.0

all: spng_x86 spng_arm64
	lipo -create -output libspng.a libspng_x86.a libspng_arm64.a
	rm libspng_x86.a libspng_arm64.a

spng_x86: ${SPNGDIR}/spng.c ${MINIZDIR}/miniz.c
	gcc -O3 -c -DMINIZ_NO_STDIO=1 -DSPNG_USE_MINIZ=1 \
		-stdlib=libc++ \
		-I${SPNGDIR} -I${MINIZDIR} ${SPNGDIR}/spng.c ${MINIZDIR}/miniz.c \
		-target x86_64-apple-macos10.9
	ar rcs libspng_x86.a spng.o miniz.o
	rm *.o

spng_arm64:
	gcc -O3 -c -DMINIZ_NO_STDIO=1 -DSPNG_USE_MINIZ=1 \
		-stdlib=libc++ \
		-I${SPNGDIR} -I${MINIZDIR} ${SPNGDIR}/spng.c ${MINIZDIR}/miniz.c \
		-target arm64-apple-macos11
	ar rcs libspng_arm64.a spng.o miniz.o
	rm *.o

clean:
	rm *.o *.a