LOCKIN
=======

**(We will soon cleanup the LOCKIN code further)**

LOCKIN is a lock library that includes several lock algorithm implementations mainly in header files for ease of use. In particular, LOCKIN includes the `lock_in.h` header file that can be used to (i) select one of LOCKIN's lock libraries, and (ii) to overload the traditional `pthread_mutex_lock` interface. Essentially, you can use LOCKIN to easily modify the pthread mutex locks in a system with practically zero effort.

Only CLH and MCS locks have corresponding source files, thus applications that use one of these two locks must link with `liblockin.a` (`-llockin`).


* Website             : http://lpd.epfl.ch/site/lockin
* Author              : Vasileios Trigonakis <vasileios.trigonakis@epfl.ch>,
* Related Publications:
  * [*Unlocking Energy*](https://www.usenix.org/conference/atc16/technical-sessions/presentation/falsafi),  
    Babak Falsafi, Rachid Guerraoui, Javier Picorel, Vasileios Trigonakis (alphabetical order),  
  USENIX ATC 2016


Lock Algorithms
---------------

You can select the lock algorithm to use in `lock_in.h`, or by passing the `LOCK_IN` compilation flag.

- `MUTEX`: the traditional phtread mutex lock;
- `MUTEXADAP`: `MUTEX` with `PTHREAD_MUTEX_ADAPTIVE_NP` enabled;
- `TAS`: the test-and-set spinlock algorithm;
- `TTAS`: the test-and-test-and-set spinlock algorithm;
- `TICKET`: the ticket spinlock algorithm;
- `MCS`: the MCS spinlock algorithm;
- `CLH`: the CLH spinlock algorithm;
- `MUTEXEE`: our new optimized `MUTEX` algorithm;
- `MUTEXEEF`: `MUTEXEE` with bounded maximum tail latencies; 
- `LOCKPROF`: a simple lock profiler that prints stats about contention.

`lock_in.h` includes more lock implementations (experimental).

In our tests, you can choose the lock algorithm by invoking:  
`make LOCK_IN=TICKET`


Compilation Options
-------------------

* `LOCK_IN=lockname` see above;
* `DEBUG=1` to enable compilation with debug symbols and `-O0`;
* `PAUSE_IN=pausetype` to change the pausing technique (see `lock_in.h`);
* `POWER=0` to disable power measurements;
* `TIMEOUT=value-ns` to configure the timeout of `MUTEXEEF` lock;

For example, `make LOCK_IN=TAS POWER=0` will build the stress tests (see below) with TAS lock and no power measurements.


Benchmarks
----------

LOCKIN includes a plethora of benchmarks. The main ones are:
* `stress_one_in` to evaluate throughput and energy efficiency of one lock;
* `stress_test_in` to evaluate throughput and energy efficiency of `-lN` locks;
* `stress_latency_in` to evaluate throughput, latency, and energy efficiency of `-lN` locks;
* `stress_ldi_in` to evaluate throughput, latency distribution, and energy efficiency of `-lN` locks;
* `stress_correct_in` to test the correctness of lock algorithms.

Take a look in the `bmarks` folder for many more tests!


Dependencies
------------

To measure power on new Intel processors (e.g., Intel Ivy Bridge), the raplread library is required (https://github.com/LPD-EPFL/raplread). By default, compiling the stress tests of LOCKIN will download and build raplread. However, you might need to manually configure raplread for your multi-core. 


Scripts
-------

You can find many useful scripts in the `scripts` folder. In particular:
* `scripts/atc` includes the scripts that we used to run the experiments for our ATC paper;
* `scripts/make_*`scripts can be used to compile various tests (e.g., `make_all_stress_tests.sh`).


Configuration
-------------

If you want thread pinning to work properly, you need to setup your platform in `platform_defs.h` (see for example the `XEON2` platform). You will then need to pass the name of the platform in the Makefile (again, look for `XEON2` as an example.
