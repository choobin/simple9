#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include "simple9.h"

struct list {
    uint32_t *data;
    size_t n;
};

static struct list *generate_list(size_t nitems, size_t max_value)
{
    struct list *list;
    size_t i;

    list = malloc(sizeof *list);
    assert(list);

    list->data = malloc(nitems * sizeof(uint32_t));
    assert(list->data);

    list->n = nitems;

    for (i = 0; i < nitems; i++)
        list->data[i] = (rand() % max_value);

    return list;
}

static void free_list(struct list *list)
{
    free(list->data);
    free(list);
}

typedef size_t (*decode_fn_t)(uint32_t**, size_t*, FILE*);

#define TMPFILE "tmpfile"

static void test(struct list *list, decode_fn_t fn)
{
    FILE *fp;
    uint32_t *data;
    size_t n;
    size_t i;

    fp = fopen(TMPFILE, "w");
    assert(fp);

    simple9_encode(list->data, list->n, fp);

    fclose(fp);

    fp = fopen(TMPFILE, "r");
    assert(fp);

    data = NULL;

    fn(&data, &n, fp);

    fclose(fp);

    if (n != list->n)
        fprintf(stderr, "[FAILED] n == %lu. It should equal %lu\n",
                (unsigned long)n, (unsigned long)list->n);

    for (i = 0; i < n; i++) {
        if (data[i] != list->data[i]) {
            fprintf(stderr,
                    "[FAILED] data[%lu] = %u. It should equal %u\n",
                    (unsigned long)i, data[i], list->data[i]);

            exit(EXIT_FAILURE);
        }
    }

    free(data);

    unlink(TMPFILE);
}

int main()
{
    struct list *list;
    size_t test_nitems[] = {32, 128, 1024, 1048576, 10485760};
    size_t test_max_value[] = {4, 8, 32, 128, 512, 16384, 262144, 268435456};
    size_t i;
    size_t j;

    srand(time(NULL));

    for (i = 0; i < sizeof test_nitems / sizeof test_nitems[0]; i++) {
        for (j = 0; j < sizeof test_max_value / sizeof test_max_value[0]; j++) {
            printf("nitems: %lu, max_value: %lu\n",
                   (unsigned long)test_nitems[i],
                   (unsigned long)test_max_value[j]);

            list = generate_list(test_nitems[i], test_max_value[j]);

            test(list, simple9_decode);

            test(list, simple9_decode_unrolled);

            free_list(list);
        }
    }

    return EXIT_SUCCESS;
}
