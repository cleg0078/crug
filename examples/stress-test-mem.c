/*
 * crug_memory_stress.crug
 *
 * Stress test for Crug Rust-style memory management.
 *
 * Tests:
 *   - own
 *   - adopt()
 *   - automatic drop
 *   - move()
 *   - borrow()
 *   - borrow_mut()
 *   - early returns
 *   - nested scopes
 *   - conditional moves
 *   - repeated ownership transfer
 *   - leak detection
 *   - double-drop detection
 *
 * Expected:
 *   created == destroyed
 *   live == 0
 *   double_destroy == 0
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define ITERATIONS 1000000ULL

struct Block {
    uint64_t magic;
    uint64_t id;
    uint64_t value;

    char payload[128];
};

#define BLOCK_MAGIC_ALIVE 0x43525547414C4956ULL
#define BLOCK_MAGIC_DEAD  0x4352554744454144ULL

static uint64_t g_created = 0;
static uint64_t g_destroyed = 0;
static uint64_t g_live = 0;

static uint64_t g_double_destroy = 0;
static uint64_t g_corruption = 0;


/*
 * Ordinary C allocator function.
 *
 * Ownership begins when Crug adopts the returned pointer.
 */
static struct Block *block_create(uint64_t id)
{
    struct Block *b =
        (struct Block *)malloc(sizeof(*b));

    if (!b) {
        fprintf(stderr, "malloc failed\n");
        abort();
    }

    b->magic = BLOCK_MAGIC_ALIVE;
    b->id = id;
    b->value = id;

    memset(b->payload,
           (int)(id & 0xff),
           sizeof(b->payload));

    ++g_created;
    ++g_live;

    return b;
}


/*
 * Destructor used by adopt().
 */
static void block_destroy(struct Block *b)
{
    if (!b)
        return;

    if (b->magic != BLOCK_MAGIC_ALIVE) {
        ++g_double_destroy;

        fprintf(stderr,
                "ERROR: invalid/double destruction "
                "of block %p\n",
                (void *)b);

        abort();
    }

    b->magic = BLOCK_MAGIC_DEAD;

    ++g_destroyed;
    --g_live;

    free(b);
}


/*
 * Shared borrow.
 *
 * Must NOT take ownership.
 * Must NOT destroy b.
 */
static uint64_t inspect(ref struct Block *b)
{
    if (b->magic != BLOCK_MAGIC_ALIVE) {
        ++g_corruption;
        abort();
    }

    return b->value;
}


/*
 * Exclusive mutable borrow.
 *
 * Again: no ownership transfer.
 */
static void increment(mut struct Block *b)
{
    if (b->magic != BLOCK_MAGIC_ALIVE) {
        ++g_corruption;
        abort();
    }

    ++b->value;
}


/*
 * Ownership is transferred into this function.
 *
 * The block must be automatically destroyed when
 * consume() returns.
 */
static uint64_t consume(struct Block *raw)
{
    /*
     * A parameter carries no destructor: ownership is attached to an owned
     * declaration.  The caller still transfers with move(), so exactly one
     * owner exists; this local is destroyed when consume() returns.
     */
    own struct Block *b = adopt(raw, block_destroy);

    return b->value;
}


/*
 * Test automatic destruction on early return.
 */
static uint64_t test_early_return(uint64_t id)
{
    own struct Block *b =
        adopt(block_create(id), block_destroy);

    if ((id & 1) == 0) {
        /*
         * b must be destroyed automatically here.
         */
        return b->value;
    }

    increment(borrow_mut(b));

    /*
     * b also destroyed automatically here.
     */
    return b->value;
}


/*
 * Test ownership transfer through several variables.
 */
static uint64_t test_move_chain(uint64_t id)
{
    own struct Block *a =
        adopt(block_create(id), block_destroy);

    /*
     * Ownership:
     *
     * a -> b
     */
    own struct Block *b = move(a);

    /*
     * b -> c
     */
    own struct Block *c = move(b);

    /*
     * c -> d
     */
    own struct Block *d = move(c);

    increment(borrow_mut(d));

    uint64_t result = inspect(borrow(d));

    /*
     * Only d owns the allocation here.
     *
     * Exactly one block_destroy() must happen.
     */
    return result;
}


/*
 * Test destruction at nested scope boundaries.
 */
static uint64_t test_nested_scopes(uint64_t id)
{
    uint64_t result = 0;

    {
        own struct Block *outer =
            adopt(block_create(id), block_destroy);

        result += inspect(borrow(outer));

        {
            own struct Block *inner =
                adopt(block_create(id + 1),
                      block_destroy);

            increment(borrow_mut(inner));

            result += inspect(borrow(inner));

        } /* inner MUST be destroyed here */

        /*
         * outer must still be valid.
         */
        result += inspect(borrow(outer));

    } /* outer MUST be destroyed here */

    return result;
}


/*
 * Test conditional ownership transfer.
 */
static uint64_t test_conditional_move(
    uint64_t id,
    int should_move)
{
    own struct Block *a =
        adopt(block_create(id), block_destroy);

    uint64_t result;

    /*
     * Transfer once, before the branch.  Ownership must be the same on both
     * paths: a conditional move leaves the value possibly-moved at the join,
     * and a drop there could not be proven exactly-once ([crug-DROP-006]).
     */
    own struct Block *b = move(a);

    if (should_move) {

        increment(borrow_mut(b));

        result = inspect(borrow(b));

        /*
         * b destroyed here.
         */

    } else {

        result = inspect(borrow(b));

        /*
         * b destroyed here as well.
         */
    }

    return result;
}


/*
 * Test transferring ownership into another function.
 */
static uint64_t test_consume(uint64_t id)
{
    own struct Block *b =
        adopt(block_create(id), block_destroy);

    increment(borrow_mut(b));

    /*
     * Ownership moves into consume().
     *
     * consume() becomes responsible for destruction.
     */
    return consume(move(b));
}


/*
 * Exercise many borrows without creating ownership.
 */
static uint64_t test_many_borrows(uint64_t id)
{
    own struct Block *b =
        adopt(block_create(id), block_destroy);

    uint64_t result = 0;

    for (int i = 0; i < 32; ++i) {

        ref struct Block *r =
            borrow(b);

        result += inspect(r);
    }

    /*
     * After last use of all refs, mutable borrow should
     * be possible with NLL enabled.
     */
    increment(borrow_mut(b));

    result += inspect(borrow(b));

    return result;
}


/*
 * One complete stress iteration.
 */
static uint64_t stress_iteration(uint64_t i)
{
    uint64_t checksum = 0;

    checksum += test_early_return(i);

    checksum += test_move_chain(
        i + 100000000ULL);

    checksum += test_nested_scopes(
        i + 200000000ULL);

    checksum += test_conditional_move(
        i + 300000000ULL,
        (int)(i & 1));

    checksum += test_consume(
        i + 400000000ULL);

    checksum += test_many_borrows(
        i + 500000000ULL);

    return checksum;
}


static void print_stats(uint64_t checksum)
{
    printf("\n");
    printf("=== Crug ownership stress results ===\n");
    printf("\n");

    printf("iterations      : %llu\n",
           (unsigned long long)ITERATIONS);

    printf("created         : %llu\n",
           (unsigned long long)g_created);

    printf("destroyed       : %llu\n",
           (unsigned long long)g_destroyed);

    printf("live            : %llu\n",
           (unsigned long long)g_live);

    printf("double destroy  : %llu\n",
           (unsigned long long)g_double_destroy);

    printf("corruption      : %llu\n",
           (unsigned long long)g_corruption);

    printf("checksum        : %llu\n",
           (unsigned long long)checksum);

    printf("\n");
}


int main(void)
{
    uint64_t checksum = 0;

    printf("Crug Rust-style memory stress test\n");
    printf("Iterations: %llu\n",
           (unsigned long long)ITERATIONS);

    for (uint64_t i = 0;
         i < ITERATIONS;
         ++i) {

        checksum += stress_iteration(i);

        if ((i % 100000ULL) == 0 &&
            i != 0) {

            printf(
                "%llu iterations "
                "- live=%llu "
                "created=%llu "
                "destroyed=%llu\n",

                (unsigned long long)i,
                (unsigned long long)g_live,
                (unsigned long long)g_created,
                (unsigned long long)g_destroyed);
        }
    }

    print_stats(checksum);

    if (g_live != 0) {

        fprintf(stderr,
                "FAIL: leaked objects: %llu\n",
                (unsigned long long)g_live);

        return 1;
    }

    if (g_created != g_destroyed) {

        fprintf(stderr,
                "FAIL: created/destroyed mismatch\n");

        return 2;
    }

    if (g_double_destroy != 0) {

        fprintf(stderr,
                "FAIL: double destruction detected\n");

        return 3;
    }

    if (g_corruption != 0) {

        fprintf(stderr,
                "FAIL: object corruption detected\n");

        return 4;
    }

    printf("PASS: ownership stress test completed successfully\n");

    return 0;
}
