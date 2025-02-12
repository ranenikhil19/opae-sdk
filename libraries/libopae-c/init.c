// Copyright(c) 2017-2022, Intel Corporation
//
// Redistribution  and  use  in source  and  binary  forms,  with  or  without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of  source code  must retain the  above copyright notice,
//   this list of conditions and the following disclaimer.
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
// * Neither the name  of Intel Corporation  nor the names of its contributors
//   may be used to  endorse or promote  products derived  from this  software
//   without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING,  BUT NOT LIMITED TO,  THE
// IMPLIED WARRANTIES OF  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED.  IN NO EVENT  SHALL THE COPYRIGHT OWNER  OR CONTRIBUTORS BE
// LIABLE  FOR  ANY  DIRECT,  INDIRECT,  INCIDENTAL,  SPECIAL,  EXEMPLARY,  OR
// CONSEQUENTIAL  DAMAGES  (INCLUDING,  BUT  NOT LIMITED  TO,  PROCUREMENT  OF
// SUBSTITUTE GOODS OR SERVICES;  LOSS OF USE,  DATA, OR PROFITS;  OR BUSINESS
// INTERRUPTION)  HOWEVER CAUSED  AND ON ANY THEORY  OF LIABILITY,  WHETHER IN
// CONTRACT,  STRICT LIABILITY,  OR TORT  (INCLUDING NEGLIGENCE  OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,  EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif // HAVE_CONFIG_H
#define _GNU_SOURCE
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <linux/limits.h>
#include <pwd.h>
#include <unistd.h>
#ifndef __USE_GNU
#define __USE_GNU
#endif // __USE_GNU
#include <pthread.h>

#include <opae/init.h>
#include <opae/utils.h>
#include "pluginmgr.h"
#include "opae_int.h"
#include "mock/opae_std.h"

/* global loglevel */
static int g_loglevel = OPAE_DEFAULT_LOGLEVEL;
static FILE *g_logfile;
/* mutex to protect against garbled log output */
static pthread_mutex_t log_lock = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;

#define CFG_PATH_MAX 64
#define HOME_CFG_PATHS 3
STATIC const char _ase_home_cfg_files[HOME_CFG_PATHS][CFG_PATH_MAX] = {
	{ "/.local/opae_ase.cfg" },
	{ "/.local/opae/opae_ase.cfg" },
	{ "/.config/opae/opae_ase.cfg" },
};
#define SYS_CFG_PATHS 2
STATIC const char _ase_sys_cfg_files[SYS_CFG_PATHS][CFG_PATH_MAX] = {
	{ "/usr/local/etc/opae/opae_ase.cfg" },
	{ "/etc/opae/opae_ase.cfg" },
};

void opae_print(int loglevel, const char *fmt, ...)
{
	FILE *fp;
	int err;
	va_list argp;

	if (loglevel > g_loglevel)
		return;

	if (loglevel == OPAE_LOG_ERROR)
		fp = stderr;
	else
		fp = g_logfile == NULL ? stdout : g_logfile;

	va_start(argp, fmt);
	err = pthread_mutex_lock(
		&log_lock); /* ignore failure and print anyway */
	if (err)
		fprintf(stderr, "pthread_mutex_lock() failed: %s",
			strerror(err));
	vfprintf(fp, fmt, argp);
	err = pthread_mutex_unlock(&log_lock);
	if (err)
		fprintf(stderr, "pthread_mutex_unlock() failed: %s",
			strerror(err));
	va_end(argp);
}

/* Find the canonicalized configuration file opae_ase.cfg. If null, the file
   was not found. Otherwise, it's the first configuration file found from a
   list of possible paths. Note: The char * returned is allocated here, caller
   must free. */
STATIC char *find_ase_cfg(void)
{
	int i = 0;
	char *file_name = NULL;
	char *opae_path = NULL;
	char cfg_path[PATH_MAX] = { 0, };
	char home_cfg[PATH_MAX] = { 0, };
	size_t len;

	// get the user's home directory
	struct passwd *user_passwd = getpwuid(getuid());

	// first look in the OPAE source directory
	file_name = opae_canonicalize_file_name(OPAE_ASE_CFG_SRC_PATH);
	if (file_name)
		return file_name;

	// second look in OPAE installation directory
	file_name = opae_canonicalize_file_name(OPAE_ASE_CFG_INST_PATH);
	if (file_name)
		return file_name;

	// third look in the release directory
	opae_path = getenv("OPAE_PLATFORM_ROOT");
	if (opae_path) {

		if (snprintf(cfg_path, sizeof(cfg_path),
			 "%s/share/opae/ase/opae_ase.cfg", opae_path) < 0) {
			OPAE_ERR("snprintf buffer overflow");
		} else {
			file_name = opae_canonicalize_file_name(cfg_path);
			if (file_name)
				return file_name;
		}
	}

	// fourth look in possible paths in the users home directory
	if (user_passwd != NULL) {
		for (i = 0; i < HOME_CFG_PATHS; ++i) {
			if (snprintf(home_cfg, sizeof(home_cfg),
				     "%s%s", user_passwd->pw_dir,
				     _ase_home_cfg_files[i]) < 0) {
				OPAE_ERR("snprintf buffer overflow");
			} else {
				file_name = opae_canonicalize_file_name(home_cfg);
				if (file_name)
					return file_name;
			}
		}
	}

	// now look in possible system paths
	for (i = 0; i < SYS_CFG_PATHS; ++i) {
		len = strnlen(_ase_sys_cfg_files[i], CFG_PATH_MAX - 1);
		memcpy(home_cfg, _ase_sys_cfg_files[i], len);
		home_cfg[len] = '\0';
		file_name = opae_canonicalize_file_name(home_cfg);
		if (file_name)
			return file_name;
	}

	return NULL;
}

extern int initialized;
__attribute__((constructor(1000))) STATIC void opae_init(void)
{
	fpga_result res;
	g_logfile = NULL;
	char *cfg_path = NULL;
	char *with_ase = NULL;

	if (initialized)
		return;

	/* try to read loglevel from environment */
	char *s = getenv("LIBOPAE_LOG");
	if (s) {
		g_loglevel = atoi(s);
#ifndef LIBOPAE_DEBUG
		if (g_loglevel >= OPAE_LOG_DEBUG)
			fprintf(stderr,
				"WARNING: Environment variable LIBOPAE_LOG is "
				"set to output debug\nmessages, "
				"but libopae-c was not built with debug "
				"information.\n");
#endif
	}

	s = getenv("LIBOPAE_LOGFILE");
	if (s) {
		if (s[0] != '/' || !strncmp(s, "/tmp/", 5)) {
			g_logfile = opae_fopen(s, "w");
			if (g_logfile == NULL) {
				fprintf(stderr,
					"Could not open log file for writing: %s. ", s);
				fprintf(stderr, "Error is: %s\n", strerror(errno));
			}
		}
	}

	if (g_logfile == NULL)
		g_logfile = stdout;

	with_ase = getenv("WITH_ASE");
	if (with_ase) {
		cfg_path = find_ase_cfg();

		if (cfg_path == NULL) {
			OPAE_ERR("WITH_ASE was set, but could not find opae_ase.cfg file");
			return;
		}

		res = fpgaInitialize(cfg_path);
		if (res != FPGA_OK)
			OPAE_ERR("fpgaInitialize: %s", fpgaErrStr(res));

		opae_free(cfg_path);
	}
	// If the environment hasn't requested explicit initialization,
	// perform the initialization implicitly here.
	else if (getenv("OPAE_EXPLICIT_INITIALIZE") == NULL)
		fpgaInitialize(NULL);
}

__attribute__((destructor)) STATIC void opae_release(void)
{
	fpga_result res = FPGA_OK;

	if (getenv("OPAE_EXPLICIT_INITIALIZE") == NULL)
		res = fpgaFinalize();

	if (res != FPGA_OK)
		OPAE_ERR("fpgaFinalize: %s", fpgaErrStr(res));

	if (g_logfile != NULL && g_logfile != stdout) {
		opae_fclose(g_logfile);
	}
	g_logfile = NULL;
}
