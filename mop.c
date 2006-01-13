#include <mono/metadata/profiler.h>
#include <mono/metadata/debug-helpers.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <dirent.h>
#include <libgen.h>


/*
 * Bare bones profiler. Compile with:
 * gcc -shared -o mono-profiler-sample.so sample.c `pkg-config --cflags --libs mono`
 * Install the binary where the dynamic loader can find it.
 * Then run mono with:
 * mono --profile=sample your_application.exe
 */

FILE* ldscript;
FILE* assm;

struct _MonoProfiler {
};

/* called at the end of the program */
static void
sample_shutdown (MonoProfiler *prof)
{
	fclose (ldscript);
	fclose (assm);
}

typedef struct _JitRange JitRange;

struct _JitRange {
	JitRange* next;
	gpointer start;
	gpointer end;
	int rangenum;
};

JitRange* range_head = NULL;
int range_num = 0;

void find_mapping_range (gpointer addr, gpointer* start, gpointer * end)
{
	const int MAX_LINE_LEN = 1024;
	FILE * fd = NULL;
	char line[MAX_LINE_LEN];
	char temp[MAX_LINE_LEN];
	int rc;


	fd = fopen("/proc/self/maps","r");
	
	if (fd) {

		while (fgets(line, MAX_LINE_LEN, fd) != NULL) {
			
			rc = sscanf(line, "%lx-%lx %s", start, end, temp);
			if (rc != 3)
				break;
						
			if (addr >= *start && addr <= *end) {
				fclose(fd);
				return;
			}
		}
	}
	*start = *end = 0;
	fclose(fd);
}


JitRange*
mop_get_jit_range (gpointer start)
{

	JitRange* r;

	for (r = range_head; r ; r = r->next) {
		if (start >= r->start && start <= r->end)
			break;
	}

	if (!r) {
		r = g_new0(JitRange, 1);
		r->next = range_head;
		range_head = r;
		
		find_mapping_range (start, &r->start, &r->end);
		r->rangenum = range_num ++;

		fprintf (ldscript, "SECTIONS { . = %p; vm%d : { *(vm%d) }}\n",
			 r->start,
			 r->rangenum,
			 r->rangenum);
	}

	return r;
}



static void
mop_jit_end (MonoProfiler *prof, MonoMethod *method, MonoJitInfo* jinfo, int result)
{
	static int n = 0;
	char* name = mono_method_full_name (method, TRUE);

	char* start = mono_jit_info_get_code_start (jinfo);
	int len = mono_jit_info_get_code_size (jinfo);
	int i;
	
	JitRange * jr;
	jr = mop_get_jit_range (start);
	
	for (i = 0; name [i]; ++i) {
		if (!isalnum (name [i]))
			name [i] = '_';
	}
	
	fprintf (assm, ".section vm%d\n", jr->rangenum);
	fprintf (assm, ".org %p\n", (char*)start - (char*)jr->start);
	fprintf (assm, "%s%d:\n", name, ++n);

	for (i = 0; i < len; i ++) {
		fprintf (assm, ".byte %d; ", start [i]);
	}
	fprintf (assm, "\n\n\n");

	g_free (name);
}

/* the entry point */
void
mono_profiler_startup (const char *desc)
{
	MonoProfiler *prof = g_new0 (MonoProfiler, 1);

	mono_profiler_install (prof, sample_shutdown);
	mono_profiler_install_jit_end (mop_jit_end);
	
	mono_profiler_set_events (MONO_PROFILE_JIT_COMPILATION);

	ldscript = fopen ("ldscript", "w");
	assm = fopen ("jit.s", "w");
}


