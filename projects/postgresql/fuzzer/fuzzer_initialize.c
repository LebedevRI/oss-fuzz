// Copyright 2020 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
///////////////////////////////////////////////////////////////////////////////

#include "postgres.h"

#include "access/xlog.h"
#include "access/xact.h"
#include "common/username.h"
#include "executor/spi.h"
#include "jit/jit.h"
#include "libpq/libpq.h"
#include "libpq/pqsignal.h"
#include "miscadmin.h"
#include "optimizer/optimizer.h"
#include "parser/analyze.h"
#include "parser/parser.h"
#include "storage/proc.h"
#include "tcop/tcopprot.h"
#include "utils/datetime.h"
#include "utils/memutils.h"
#include "utils/portal.h"
#include "utils/snapmgr.h"
#include "utils/timeout.h"

const char *progname;
static MemoryContext row_description_context = NULL;
static StringInfoData row_description_buf;
static const char *username = NULL;

static void fuzzer_exit(){
  if(username)
    pfree((void *) username);
}

int FuzzerInitialize(char *dbname){
  char *argv[5];
  char arg_path[50];
  char path_to_db[50];
  char untar[100];
  snprintf(arg_path, sizeof(arg_path), "/tmp/%s/data", dbname);
  snprintf(path_to_db, sizeof(path_to_db), "-D\"/tmp/%s/data\"", dbname);
  snprintf(untar, sizeof(untar), "rm -rf /tmp/%s && mkdir /tmp/%s && tar -xvf data.tar.gz -C /tmp/%s", dbname, dbname, dbname);
  
  argv[0] = "tmp_install/usr/local/pgsql/bin/postgres";
  argv[1] = path_to_db;
  argv[2] = "-F";
  argv[3] = "-k\"/tmp/pg_dbfuzz\"";
  argv[4] = NULL;

  system(untar);
  
  progname = get_progname(argv[0]);
  MemoryContextInit();

  username = strdup(get_user_name_or_exit(progname));
	 
  InitStandaloneProcess(argv[0]);
  SetProcessingMode(InitProcessing);
  InitializeGUCOptions();
  process_postgres_switches(4, argv, PGC_POSTMASTER, NULL);

  SelectConfigFiles(arg_path, progname);

  checkDataDir();
  ChangeToDataDir();
  CreateDataDirLockFile(false);
  LocalProcessControlFile(false);
  InitializeMaxBackends();
		 
  BaseInit();
  InitProcess();
  PG_SETMASK(&UnBlockSig);
  InitPostgres("dbfuzz", InvalidOid, username, InvalidOid, NULL, false);
 
  SetProcessingMode(NormalProcessing);

  BeginReportingGUCOptions();
  process_session_preload_libraries();

  MessageContext = AllocSetContextCreate(TopMemoryContext,
					 "MessageContext",
					 ALLOCSET_DEFAULT_SIZES);
  row_description_context = AllocSetContextCreate(TopMemoryContext,
						  "RowDescriptionContext",
						  ALLOCSET_DEFAULT_SIZES);
  MemoryContextSwitchTo(row_description_context);
  initStringInfo(&row_description_buf);
  MemoryContextSwitchTo(TopMemoryContext);

  PgStartTime = GetCurrentTimestamp();
  whereToSendOutput = DestNone;
  Log_destination = 0;
  atexit(fuzzer_exit);
  return 0;
}
