/*
Cuckoo Sandbox - Automated Malware Analysis
Copyright (C) 2010-2015 Cuckoo Sandbox Developers, Optiv, Inc. (brad.spengler@optiv.com)

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include "ntapi.h"
#include "config.h"
#include "misc.h"
#include "log.h"
#include "hooking.h"

extern void DoOutputDebugString(_In_ LPCTSTR lpOutputString, ...);
extern PVOID bp0, bp1, bp2, bp3;
#ifdef CAPE_TRACE
#define BP_EXEC        0x00
#define BP_WRITE       0x01
#define BP_RESERVED    0x02
#define BP_READWRITE   0x03
#define DoClearZeroFlag 1
#define DoSetZeroFlag   2
#define PrintEAX        3
extern int TraceDepthLimit, EntryPointRegister;
extern unsigned int StepLimit, Action0, Action1, Action2, Action3;
extern unsigned int Type0, Type1, Type2, Type3;
extern char *Instruction0, *Instruction1, *Instruction2, *Instruction3;
#endif

int read_config(void)
{
    // TODO unicode support
    char buf[32768], config_fname[MAX_PATH];
	FILE *fp;
	unsigned int i;
	unsigned int vallen;

    sprintf(config_fname, "C:\\%u.ini", GetCurrentProcessId());

    fp = fopen(config_fname, "r");
	if (fp == NULL) {
		// for debugging purposes
		fp = fopen("C:\\config.ini", "r");
		if (fp == NULL)
			return 0;
	}

	g_config.force_sleepskip = -1;
#ifdef _WIN64
	g_config.hook_type = HOOK_JMP_INDIRECT;
#else
	g_config.hook_type = HOOK_HOTPATCH_JMP_INDIRECT;
#endif
    g_config.procdump = 0;
    g_config.procmemdump = 0;

#ifdef CAPE_TRACE
    EntryPointRegister = 0;
#endif

	memset(buf, 0, sizeof(buf));
	while (fgets(buf, sizeof(buf), fp) != NULL)
	{
        // cut off the newline
        char *p = strchr(buf, '\r');
        if(p != NULL) *p = 0;
        p = strchr(buf, '\n');
        if(p != NULL) *p = 0;

        // split key=value
        p = strchr(buf, '=');
        if(p != NULL) {
			const char *key = buf;
			char *value = p + 1;

			*p = 0;
			vallen = (unsigned int)strlen(value);
            if(!stricmp(key, "pipe")) {
				for (i = 0; i < vallen; i++)
					g_config.pipe_name[i] = (wchar_t)(unsigned short)value[i];
            }
			else if (!stricmp(key, "logserver")) {
				strncpy(g_config.logserver, value,
					ARRAYSIZE(g_config.logserver));
			}
			else if (!stricmp(key, "results")) {
                strncpy(g_config.results, value,
                    ARRAYSIZE(g_config.results) - 1);
				for (i = 0; i < ARRAYSIZE(g_config.results); i++)
					g_config.w_results[i] = (wchar_t)(unsigned short)g_config.results[i];
			}
			else if (!stricmp(key, "file-of-interest")) {
				unsigned int len = (unsigned int)strlen(value);
				if (len > 1) {
					if (value[1] == ':') {
						// is a file
						char *tmp = calloc(1, MAX_PATH);

						ensure_absolute_ascii_path(tmp, value);
						g_config.file_of_interest = ascii_to_unicode_dup(tmp);
						free(tmp);
						// if the file of interest is our own executable, then don't do any special handling
						if (wcsicmp(our_process_path, g_config.file_of_interest))
							g_config.suspend_logging = TRUE;
					}
					else {
						// is a URL
						g_config.url_of_interest = ascii_to_unicode_dup(value);
						g_config.suspend_logging = TRUE;
					}
				}
			}
			else if (!stricmp(key, "referrer")) {
				g_config.w_referrer = ascii_to_unicode_dup(value);
				g_config.referrer = strdup(value);
			}
			else if (!stricmp(key, "analyzer")) {
                strncpy(g_config.analyzer, value,
                    ARRAYSIZE(g_config.analyzer)-1);
				for (i = 0; i < ARRAYSIZE(g_config.analyzer); i++)
					g_config.w_analyzer[i] = (wchar_t)(unsigned short)g_config.analyzer[i];
				wcscpy(g_config.dllpath, g_config.w_analyzer);
				if (wcslen(g_config.dllpath) < ARRAYSIZE(g_config.dllpath) - 5)
					wcscat(g_config.dllpath, L"\\dll\\");
            }
            else if(!stricmp(key, "shutdown-mutex")) {
                strncpy(g_config.shutdown_mutex, value,
                    ARRAYSIZE(g_config.shutdown_mutex));
            }
            else if(!stricmp(key, "first-process")) {
                g_config.first_process = value[0] == '1';
            }
            else if(!stricmp(key, "startup-time")) {
                g_config.startup_time = atoi(value);
            }
			else if (!stricmp(key, "debug")) {
				g_config.debug = atoi(value);
			}
			else if (!stricmp(key, "hook-type")) {
#ifndef _WIN64
				if (!stricmp(value, "direct"))
					g_config.hook_type = HOOK_JMP_DIRECT;
				else if (!stricmp(value, "indirect"))
					g_config.hook_type = HOOK_JMP_INDIRECT;
				else if (!stricmp(value, "safe"))
					g_config.hook_type = HOOK_SAFEST;
#endif
			}
			else if (!stricmp(key, "disable_hook_content")) {
				g_config.disable_hook_content = atoi(value);
			}
			/*
			else if(!stricmp(key, "host-ip")) {
                g_config.host_ip = inet_addr(value);
            }
            else if(!stricmp(key, "host-port")) {
                g_config.host_port = atoi(value);
            }
			*/
            else if(!stricmp(key, "force-sleepskip")) {
                g_config.force_sleepskip = value[0] == '1';
            }
			else if (!stricmp(key, "serial")) {
				g_config.serial_number = (unsigned int)strtoul(value, NULL, 16);
			}
			else if (!stricmp(key, "sysvol_ctimelow")) {
				g_config.sysvol_ctime.dwLowDateTime = (unsigned int)strtoul(value, NULL, 16);
			}
			else if (!stricmp(key, "sysvol_ctimehigh")) {
				g_config.sysvol_ctime.dwHighDateTime = (unsigned int)strtoul(value, NULL, 16);
			}
			else if (!stricmp(key, "sys32_ctimelow")) {
				g_config.sys32_ctime.dwLowDateTime = (unsigned int)strtoul(value, NULL, 16);
			}
			else if (!stricmp(key, "sys32_ctimehigh")) {
				g_config.sys32_ctime.dwHighDateTime = (unsigned int)strtoul(value, NULL, 16);
			}
			else if (!stricmp(key, "full-logs")) {
				g_config.full_logs = value[0] == '1';
			}
			else if (!stricmp(key, "force-flush")) {
				g_config.force_flush = atoi(value);
			}
			else if (!stricmp(key, "terminate-event")) {
				strncpy(g_config.terminate_event_name, value,
					ARRAYSIZE(g_config.terminate_event_name));
			}
			else if (!stricmp(key, "no-stealth")) {
				g_config.no_stealth = value[0] == '1';
			}
			else if (!stricmp(key, "buffer-max")) {
				buffer_log_max = (unsigned int)strtoul(value, NULL, 10);
			}
			else if (!stricmp(key, "large-buffer-max")) {
				large_buffer_log_max = (unsigned int)strtoul(value, NULL, 10);
			}
			else if (!stricmp(key, "exclude-apis")) {
				unsigned int x = 0;
				char *p2;
				p = value;
				while (p && x < EXCLUSION_MAX) {
					p2 = strchr(p, ':');
					if (p2) {
						*p2 = '\0';
					}
					g_config.excluded_apinames[x++] = strdup(p);
					if (p2 == NULL)
						break;
					p = p2 + 1;
				}
			}
			else if (!stricmp(key, "exclude-dlls")) {
				unsigned int x = 0;
				char *p2;
				p = value;
				while (p && x < EXCLUSION_MAX) {
					p2 = strchr(p, ':');
					if (p2) {
						*p2 = '\0';
					}
					g_config.excluded_dllnames[x++] = ascii_to_unicode_dup(p);
					if (p2 == NULL)
						break;
					p = p2 + 1;
				}
			}
			else if (!stricmp(key, "base-on-api")) {
				unsigned int x = 0;
				char *p2;
				p = value;
				while (p && x < EXCLUSION_MAX) {
					p2 = strchr(p, ':');
					if (p2) {
						*p2 = '\0';
					}
					g_config.base_on_apiname[x++] = strdup(p);
                    DoOutputDebugString("Config: Added '%s' to base-on-API list.\n", p);
					if (p2 == NULL)
						break;
					p = p2 + 1;
				}
			}
			else if (!stricmp(key, "dump-on-api")) {
				unsigned int x = 0;
				char *p2;
				p = value;
				while (p && x < EXCLUSION_MAX) {
					p2 = strchr(p, ':');
					if (p2) {
						*p2 = '\0';
					}
					g_config.dump_on_apinames[x++] = strdup(p);
                    DoOutputDebugString("Config: Added '%s' to dump-on-API list.\n", p);
					if (p2 == NULL)
						break;
					p = p2 + 1;
				}
			}
#ifdef CAPE_TRACE
            else if (!stricmp(key, "bp0")) {
				char *p2;
				p = value;
                p2 = strchr(p, ':');
                if (p2 && *(p2+1) == ':') {
                    bp0 = 0;
                    *p2 = '\0';
                    *(p2+1) = '\0';
                    HANDLE Module = GetModuleHandle(value);
                    if (Module)
                        bp0 = GetProcAddress(Module, p2+2);
                    else
                        DoOutputDebugString("Config: Failed to get base for module (%s).\n", value);
                    if (bp0) {
                        DoOutputDebugString("Config: bp0 set to 0x%p (%s::%s).\n", bp0, value, p2+2);
                    }
                    else
                        DoOutputDebugString("Config: Failed to get address for function %s::%s.\n", value, p2+2);
                }
                else if (!strncmp(value, "ep", 2)) {
                    DoOutputDebugString("Config: bp0 set to entry point.\n", bp0);
                    EntryPointRegister = 1;
                }
                else {
                    bp0 = (PVOID)(DWORD_PTR)strtoul(value, NULL, 0);
                    DoOutputDebugString("Config: bp0 set to 0x%x.\n", bp0);
                }
			}
            else if (!stricmp(key, "bp1")) {
                if (!strncmp(value, "ep", 2)) {
                    DoOutputDebugString("Config: bp1 set to entry point.\n", bp1);
                    EntryPointRegister = 2;
                }
                else {
                    bp1 = (PVOID)(DWORD_PTR)strtoul(value, NULL, 0);
                    DoOutputDebugString("Config: bp1 set to 0x%x.\n", bp1);
                }
			}
            else if (!stricmp(key, "bp2")) {
                if (!strncmp(value, "ep", 2)) {
                    DoOutputDebugString("Config: bp2 set to entry point.\n", bp2);
                    EntryPointRegister = 3;
                }
                else {
                    bp2 = (PVOID)(DWORD_PTR)strtoul(value, NULL, 0);
                    DoOutputDebugString("Config: bp2 set to 0x%x.\n", bp2);
                }
			}
            else if (!stricmp(key, "bp3")) {
                if (!strncmp(value, "ep", 2)) {
                    DoOutputDebugString("Config: bp3 set to entry point.\n", bp3);
                    EntryPointRegister = 4;
                }
                else {
                    bp3 = (PVOID)(DWORD_PTR)strtoul(value, NULL, 0);
                    DoOutputDebugString("Config: bp3 set to 0x%x.\n", bp3);
                }
			}
            else if (!stricmp(key, "depth")) {
				TraceDepthLimit = (int)strtoul(value, NULL, 10);
                DoOutputDebugString("Config: Trace depth set to 0x%x", TraceDepthLimit);
			}
            else if (!stricmp(key, "count")) {
				StepLimit = (unsigned int)strtoul(value, NULL, 10);
                DoOutputDebugString("Config: Trace instruction count set to 0x%x", StepLimit);
			}
            else if (!stricmp(key, "action0")) {
				if (!stricmp(value, "ClearZeroFlag")){
                    Action0 = DoClearZeroFlag;
                    DoOutputDebugString("Config: Action0 set to DoClearZeroFlag.");
                }
				else if (!stricmp(value, "SetZeroFlag")){
                    Action0 = DoSetZeroFlag;
                    DoOutputDebugString("Config: Action0 set to DoSetZeroFlag.");
                }
				else if (!stricmp(value, "PrintEAX")){
                    Action0 = PrintEAX;
                    DoOutputDebugString("Config: Action0 set to PrintEAX.");
                }
			}
            else if (!stricmp(key, "action1")) {
				if (!stricmp(value, "ClearZeroFlag")){
                    Action1 = DoClearZeroFlag;
                    DoOutputDebugString("Config: Action1 set to DoClearZeroFlag.");
                }
				else if (!stricmp(value, "SetZeroFlag")){
                    Action1 = DoSetZeroFlag;
                    DoOutputDebugString("Config: Action1 set to DoSetZeroFlag.");
                }
			}
            else if (!stricmp(key, "instruction0")) {
                Instruction0 = calloc(1, MAX_PATH);
                strncpy(Instruction0, value, strlen(value));
                DoOutputDebugString("Config: Instruction0 set to %s.", value);
			}
            else if (!stricmp(key, "instruction1")) {
                Instruction1 = calloc(1, MAX_PATH);
                strncpy(Instruction1, value, strlen(value));
                DoOutputDebugString("Config: Instruction1 set to %s.", value);
			}
            else if (!stricmp(key, "break-on-return")) {
				strncpy(g_config.break_on_return, value, ARRAYSIZE(g_config.break_on_return));
                DoOutputDebugString("Config: Break-on-return set to %s.", g_config.break_on_return);
			}
            else if (!stricmp(key, "trace-all")) {
				g_config.trace_all = value[0] == '1';
                if (g_config.trace_all)
                    DoOutputDebugString("Config: Trace all enabled.\n");
			}
			else if (!stricmp(key, "trace-into-api")) {
				unsigned int x = 0;
				char *p2;
				p = value;
				while (p && x < EXCLUSION_MAX) {
					p2 = strchr(p, ':');
					if (p2) {
						*p2 = '\0';
					}
					g_config.trace_into_api[x++] = strdup(p);
                    DoOutputDebugString("Config: Added '%s' to trace-into-API list.\n", p);
					if (p2 == NULL)
						break;
					p = p2 + 1;
				}
			}
            else if (!stricmp(key, "type0")) {
                if (!strnicmp(value, "w", 1)) {
                    DoOutputDebugString("Config: Breakpoint 0 type set to write (Type0 = BP_WRITE).\n");
                    Type0 = BP_WRITE;
                }
                else if (!strnicmp(value, "r", 1) || !strnicmp(value, "rw", 2)) {
                    DoOutputDebugString("Config: Breakpoint 0 type set to read/write (Type0 = BP_READWRITE).\n");
                    Type0 = BP_READWRITE;
                }
                else if (!strnicmp(value, "x", 1)) {
                    DoOutputDebugString("Config: Breakpoint 0 type set to execute (Type0 = BP_EXEC).\n");
                    Type0 = BP_EXEC;
                }
			}
#endif
            else if (!stricmp(key, "procdump")) {
				g_config.procdump = value[0] == '1';
                if (g_config.procdump)
                    DoOutputDebugString("Config: Process dumps enabled.\n");
                else
                    DoOutputDebugString("Config: Process dumps disabled.\n");
			}
            else if (!stricmp(key, "procmemdump")) {
				// for backwards compatibility with spender
                if (!stricmp(value, "yes"))
                    g_config.procmemdump = 1;
                else
                    g_config.procmemdump = value[0] == '1';
                if (g_config.procmemdump)
                    DoOutputDebugString("Config: Full process memory dumps enabled.\n");
                else
                    DoOutputDebugString("Config: Full process memory dumps disabled.\n");
			}
            else if (!stricmp(key, "import_reconstruction")) {
				g_config.import_reconstruction = value[0] == '1';
                if (g_config.import_reconstruction)
                    DoOutputDebugString("Config: Import reconstruction of process dumps enabled.\n");
                else
                    DoOutputDebugString("Config: Import reconstruction of process dumps disabled.\n");
			}
            else if (!stricmp(key, "terminate-processes")) {
				g_config.terminate_processes = value[0] == '1';
                if (g_config.terminate_processes)
                    DoOutputDebugString("Config: Terminate processes on terminate_event enabled.\n");
                else
                    DoOutputDebugString("Config: Terminate processes on terminate_event disabled.\n");
			}
            else DoOutputDebugString("Config: CAPE debug - unrecognised key %s.\n", key);
		}
    }

	/* don't suspend logging if this isn't the first process or if we want all the logs */
	if (!g_config.first_process || g_config.full_logs)
		g_config.suspend_logging = FALSE;

	fclose(fp);
    DeleteFileA(config_fname);
	return 1;
}
