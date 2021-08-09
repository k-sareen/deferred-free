# Deferred Free
This is a drop-in replacement for a standard malloc-free implementation that
artificially inserts the "inhale-exhale" behaviour of garbage collection to
manual memory management. We do this by hijacking all calls to `free()` in a C
or C++ program, and placing the objects into a local buffer instead of actually
deallocating the object. When the buffer volume, that is when the sum of sizes
of objects in the buffer, is greater than some constant we walk through the
buffer and free all objects.

This is different from using a (conservative) garbage collector such as the
[Boehm GC](https://github.com/ivmai/bdwgc) as we do not perform a transitive
closure over the entire heap to figure out what is alive or dead. Instead we
rely on the programmer to insert (hopefully correct) calls to `free()` when
objects are no longer required.

This library is not meant to be used as a proper replacement for a malloc-free
implementation, and as such general purpose usage is not supported.

## Building
The only (current) supported target is Linux. The library (and some simple test
programs) can be compiled running `make`. The resultant shared-library
(`libql`) and test binaries will be found in the `out/` directory.

## Usage
The library can be used by directly by including `ql.h`, using the exposed
`ql_` prefixed functions for allocation and deallocation, and linking with the
shared-library.

Alternatively, the library can be used to override calls to `malloc()` and
`free()` in a pre-compiled binary without changing or recompiling it as:

```
> LD_PRELOAD=/path/to/libql.so mybinary
```

In fact this approach can be used to override the malloc-free implementation of
other allocator libraries such as
[mimalloc](https://github.com/microsoft/mimalloc),
[Hoard](https://github.com/emeryberger/Hoard) etc. as well (this is the intended
use case). This can be achieved by preloading the `libql` library before the
second library:

```
> LD_PRELOAD="/path/to/libql.so /path/to/libmimalloc.so" mybinary
```

`libql` also sets the quarantine buffer volume using the environment variable
`QL_SIZE`. The default volume of the buffer if this environment variable is
unset is 40960 bytes. Changing the quarantine buffer volume can be achieved by:

```
> QL_SIZE=10000 LD_PRELOAD=/path/to/libql.so mybinary
```

## Debug Options
This library has two debug options, with one being more verbose than the other.
These can be used by changing the macros `DEBUG` and `VERBOSE` to `1` in
`src/ql.c` and then recompiling the library.
