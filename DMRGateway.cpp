/*
 *   Copyright (C) 2015-2020 by Jonathan Naylor G4KLX
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "RewriteType.h"
#include "DMRSlotType.h"
#include "RewriteSrc.h"
#include "DMRGateway.h"
#include "StopWatch.h"
#include "RewritePC.h"
#include "RewriteSrcId.h"
#include "RewriteDstId.h"
#include "PassAllPC.h"
#include "PassAllTG.h"
#include "DMRFullLC.h"
#include "XLXVoice.h"
#include "Version.h"
#include "Thread.h"
#include "DMRLC.h"
#include "Sync.h"
#include "Log.h"
#include "GitVersion.h"

#include <cstdio>
#include <vector>

#if !defined(_WIN32) && !defined(_WIN64)
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <pwd.h>
#endif

#if defined(_WIN32) || defined(_WIN64)
const char* DEFAULT_INI_FILE = "DMRGateway.ini";
#else
const char* DEFAULT_INI_FILE = "/etc/DMRGateway.ini";
#endif

const unsigned int XLX_SLOT = 2U;
const unsigned int XLX_TG   = 9U;

const unsigned char COLOR_CODE = 3U;

static bool m_killed = false;
static int  m_signal = 0;
static int mult = 0;
unsigned int rdstId = 0;
static int ctrlCode = 0;
static int selnet = 0;
unsigned int  storedtg =0;	
static bool net1ok = false;
static bool net2ok = false;
static bool net3ok = false;
static bool net4ok = false;
static bool net5ok = false;
static bool net6ok = false;
static int  locknet = 0;

void ClearNetworks();

#if !defined(_WIN32) && !defined(_WIN64)
static void sigHandler(int signum)
{
	m_killed = true;
	m_signal = signum;
}
#endif

const char* HEADER1 = "This software is for use on amateur radio networks only,";
const char* HEADER2 = "it is to be used for educational purposes only. Its use on";
const char* HEADER3 = "commercial networks is strictly prohibited.";
const char* HEADER4 = "Copyright(C) 2017-2020 by Jonathan Naylor, G4KLX and others";
const char* HEADER5 = "Network 6 added, and 7 digit isolation Code added by VE3RD";

int main(int argc, char** argv)
{
	const char* iniFile = DEFAULT_INI_FILE;

	if (argc > 1) {
		for (int currentArg = 1; currentArg < argc; ++currentArg) {
			std::string arg = argv[currentArg];
			if ((arg == "-v") || (arg == "--version")) {
				::fprintf(stdout, "DMRGateway version %s git #%.7s\n", VERSION, gitversion);
				return 0;
			} else if (arg.substr(0,1) == "-") {
				::fprintf(stderr, "Usage: DMRGateway [-v|--version] [filename]\n");
				return 1;
			} else {
				iniFile = argv[currentArg];
			}
		}
	}

#if !defined(_WIN32) && !defined(_WIN64)
	::signal(SIGINT,  sigHandler);
	::signal(SIGTERM, sigHandler);
	::signal(SIGHUP,  sigHandler);
#endif


	int ret = 0;

	do {
		m_signal = 0;

		CDMRGateway* host = new CDMRGateway(std::string(iniFile));
		ret = host->run();

		delete host;

		if (m_signal == 2)
			::LogInfo("DMRGateway-%s exited on receipt of SIGINT", VERSION);

		if (m_signal == 15)
			::LogInfo("DMRGateway-%s exited on receipt of SIGTERM", VERSION);

		if (m_signal == 1)
			::LogInfo("DMRGateway-%s restarted on receipt of SIGHUP", VERSION);
	} while (m_signal == 1);

	::LogFinalise();

	return ret;
}

CDMRGateway::CDMRGateway(const std::string& confFile) :
m_conf(confFile),
m_status(NULL),
m_repeater(NULL),
m_config(NULL),
m_configLen(0U),
m_dmrNetwork1(NULL),
m_dmr1Name(),
m_dmrNetwork2(NULL),
m_dmr2Name(),
m_dmrNetwork3(NULL),
m_dmr3Name(),
m_dmrNetwork4(NULL),
m_dmr4Name(),
m_dmrNetwork5(NULL),
m_dmr5Name(),
m_dmrNetwork6(NULL),
m_dmr6Name(),
m_xlxReflectors(NULL),
m_xlxNetwork(NULL),
m_xlxId(0U),
m_xlxNumber(0U),
m_xlxReflector(4000U),
m_xlxSlot(0U),
m_xlxTG(0U),
m_xlxBase(0U),
m_xlxLocal(0U),
m_xlxPort(62030U),
m_xlxPassword("passw0rd"),
m_xlxStartup(950U),
m_xlxRoom(4000U),
m_xlxRelink(1000U),
m_xlxConnected(false),
m_xlxDebug(false),
m_xlxUserControl(true),
m_xlxModule(),
m_rptRewrite(NULL),
m_xlxRewrite(NULL),
m_dmr1NetRewrites(),
m_dmr1RFRewrites(),
m_dmr1SrcRewrites(),
m_dmr2NetRewrites(),
m_dmr2RFRewrites(),
m_dmr2SrcRewrites(),
m_dmr3NetRewrites(),
m_dmr3RFRewrites(),
m_dmr3SrcRewrites(),
m_dmr4NetRewrites(),
m_dmr4RFRewrites(),
m_dmr4SrcRewrites(),
m_dmr5NetRewrites(),
m_dmr5RFRewrites(),
m_dmr5SrcRewrites(),
m_dmr6NetRewrites(),
m_dmr6RFRewrites(),
m_dmr6SrcRewrites(),
m_dmr1Passalls(),
m_dmr2Passalls(),
m_dmr3Passalls(),
m_dmr4Passalls(),
m_dmr5Passalls(),
m_dmr6Passalls(),
m_dynVoices(),
m_dynRF(),
m_socket(NULL)
{
	m_status = new DMRGW_STATUS[3U];
	m_status[1U] = DMRGWS_NONE;
	m_status[2U] = DMRGWS_NONE;

	m_config = new unsigned char[400U];
}

CDMRGateway::~CDMRGateway()
{
	for (std::vector<CRewrite*>::iterator it = m_dmr1NetRewrites.begin(); it != m_dmr1NetRewrites.end(); ++it)
		delete *it;

	for (std::vector<CRewrite*>::iterator it = m_dmr1RFRewrites.begin(); it != m_dmr1RFRewrites.end(); ++it)
		delete *it;

	for (std::vector<CRewrite*>::iterator it = m_dmr1SrcRewrites.begin(); it != m_dmr1SrcRewrites.end(); ++it)
		delete *it;

	for (std::vector<CRewrite*>::iterator it = m_dmr2NetRewrites.begin(); it != m_dmr2NetRewrites.end(); ++it)
		delete *it;
	
	for (std::vector<CRewrite*>::iterator it = m_dmr2RFRewrites.begin(); it != m_dmr2RFRewrites.end(); ++it)
		delete *it;

	for (std::vector<CRewrite*>::iterator it = m_dmr2SrcRewrites.begin(); it != m_dmr2SrcRewrites.end(); ++it)
		delete *it;

	for (std::vector<CRewrite*>::iterator it = m_dmr3NetRewrites.begin(); it != m_dmr3NetRewrites.end(); ++it)
		delete *it;

	for (std::vector<CRewrite*>::iterator it = m_dmr3RFRewrites.begin(); it != m_dmr3RFRewrites.end(); ++it)
		delete *it;

	for (std::vector<CRewrite*>::iterator it = m_dmr3SrcRewrites.begin(); it != m_dmr3SrcRewrites.end(); ++it)
		delete *it;

	for (std::vector<CRewrite*>::iterator it = m_dmr4NetRewrites.begin(); it != m_dmr4NetRewrites.end(); ++it)
		delete *it;

	for (std::vector<CRewrite*>::iterator it = m_dmr4RFRewrites.begin(); it != m_dmr4RFRewrites.end(); ++it)
		delete *it;

	for (std::vector<CRewrite*>::iterator it = m_dmr4SrcRewrites.begin(); it != m_dmr4SrcRewrites.end(); ++it)
		delete *it;

	for (std::vector<CRewrite*>::iterator it = m_dmr5NetRewrites.begin(); it != m_dmr5NetRewrites.end(); ++it)
		delete *it;

	for (std::vector<CRewrite*>::iterator it = m_dmr5RFRewrites.begin(); it != m_dmr5RFRewrites.end(); ++it)
		delete *it;

	for (std::vector<CRewrite*>::iterator it = m_dmr5SrcRewrites.begin(); it != m_dmr5SrcRewrites.end(); ++it)
		delete *it;

	for (std::vector<CRewrite*>::iterator it = m_dmr6NetRewrites.begin(); it != m_dmr6NetRewrites.end(); ++it)
		delete *it;

	for (std::vector<CRewrite*>::iterator it = m_dmr6RFRewrites.begin(); it != m_dmr6RFRewrites.end(); ++it)
		delete *it;

	for (std::vector<CRewrite*>::iterator it = m_dmr6SrcRewrites.begin(); it != m_dmr6SrcRewrites.end(); ++it)
		delete *it;

	for (std::vector<CRewrite*>::iterator it = m_dmr1Passalls.begin(); it != m_dmr1Passalls.end(); ++it)
		delete *it;

	for (std::vector<CRewrite*>::iterator it = m_dmr2Passalls.begin(); it != m_dmr2Passalls.end(); ++it)
		delete *it;

	for (std::vector<CRewrite*>::iterator it = m_dmr3Passalls.begin(); it != m_dmr3Passalls.end(); ++it)
		delete *it;

	for (std::vector<CRewrite*>::iterator it = m_dmr4Passalls.begin(); it != m_dmr4Passalls.end(); ++it)
		delete *it;

	for (std::vector<CRewrite*>::iterator it = m_dmr5Passalls.begin(); it != m_dmr5Passalls.end(); ++it)
		delete *it;

	for (std::vector<CRewrite*>::iterator it = m_dmr6Passalls.begin(); it != m_dmr6Passalls.end(); ++it)
		delete *it;

	for (std::vector<CDynVoice*>::iterator it = m_dynVoices.begin(); it != m_dynVoices.end(); ++it)
		delete* it;

	delete m_rptRewrite;
	delete m_xlxRewrite;

	delete[] m_status;
	delete[] m_config;
}

int CDMRGateway::run()
{
	bool ret = m_conf.read();
	if (!ret) {
		::fprintf(stderr, "DMRGateway: cannot read the .ini file\n");
		return 1;
	}

#if !defined(_WIN32) && !defined(_WIN64)
	bool m_daemon = m_conf.getDaemon();
	if (m_daemon) {
		// Create new process
		pid_t pid = ::fork();
		if (pid == -1) {
			::fprintf(stderr, "Couldn't fork() , exiting\n");
			return -1;
		} else if (pid != 0) {
			exit(EXIT_SUCCESS);
		}

		// Create new session and process group
		if (::setsid() == -1){
			::fprintf(stderr, "Couldn't setsid(), exiting\n");
			return -1;
		}

		// Set the working directory to the root directory
		if (::chdir("/") == -1){
			::fprintf(stderr, "Couldn't cd /, exiting\n");
			return -1;
		}

		// If we are currently root...
		if (getuid() == 0) {
			struct passwd* user = ::getpwnam("mmdvm");
			if (user == NULL) {
				::fprintf(stderr, "Could not get the mmdvm user, exiting\n");
				return -1;
			}
			
			uid_t mmdvm_uid = user->pw_uid;
			gid_t mmdvm_gid = user->pw_gid;

			// Set user and group ID's to mmdvm:mmdvm
			if (setgid(mmdvm_gid) != 0) {
				::fprintf(stderr, "Could not set mmdvm GID, exiting\n");
				return -1;
			}

			if (setuid(mmdvm_uid) != 0) {
				::fprintf(stderr, "Could not set mmdvm UID, exiting\n");
				return -1;
			}
		    
			// Double check it worked (AKA Paranoia) 
			if (setuid(0) != -1) {
				::fprintf(stderr, "It's possible to regain root - something is wrong!, exiting\n");
				return -1;
			}
		}
	}
#endif

	ret = ::LogInitialise(m_conf.getLogFilePath(), m_conf.getLogFileRoot(), m_conf.getLogFileLevel(), m_conf.getLogDisplayLevel());
	if (!ret) {
		::fprintf(stderr, "DMRGateway: unable to open the log file\n");
		return 1;
	}

#if !defined(_WIN32) && !defined(_WIN64)
	if (m_daemon) {
		::close(STDIN_FILENO);
		::close(STDOUT_FILENO);
		::close(STDERR_FILENO);
	}
#endif

	LogInfo(HEADER1);
	LogInfo(HEADER2);
	LogInfo(HEADER3);
	LogInfo(HEADER4);
	LogInfo(HEADER5);

	LogMessage("DMRGateway-%s is starting", VERSION);
	LogMessage("Built %s %s (GitID #%.7s)", __TIME__, __DATE__, gitversion);

	ret = createMMDVM();
	if (!ret)
		return 1;

	LogMessage("Waiting for MMDVM to connect.....");
	

	while (!m_killed) {
		m_configLen = m_repeater->getConfig(m_config);
		if (m_configLen > 0U && m_repeater->getId() > 1000U)
			break;

		m_repeater->clock(10U);

		CThread::sleep(10U);
	}

	if (m_killed) {
		m_repeater->close();
		delete m_repeater;
		return 0;
	}

	LogMessage("MMDVM has connected");

	int StartNet = m_conf.getStartNet();
	if  ( !StartNet  ) StartNet=4;

        selnet = StartNet;

        LogInfo("Network %d Selected for StartUp",selnet);
	ClearNetworks();

//      net1ok = false;
//	net2ok = false;
//	net3ok = false;
//      net4ok = false;
//	net5ok = false;
//	net6ok = false;
//
//	LogInfo("Rule trace: %s", ruleTrace ? "yes" : "no");

        switch(selnet) {
        case 1 : net1ok = true;
	LogInfo("Srartup Net1 Test %d", selnet);
		break;
        case 2 : net2ok = true;
	LogInfo("Srartup Net2 Test %d", selnet);
		break;
        case 3 : net3ok = true;
	LogInfo("Srartup Net3 Test %d", selnet);
		break;
        case 4 : net4ok = true;
	LogInfo("Srartup Net4 Test %d", selnet);
		break;
        case 5 : net5ok = true;
	LogInfo("Srartup Net5 Test %d", selnet);
		break;
        case 6 : net6ok = true;
	LogInfo("Srartup Net6 Test %d", selnet);
		break;
        }




	bool ruleTrace = m_conf.getRuleTrace();
	LogInfo("Rule trace: %s", ruleTrace ? "yes" : "no");


	if (m_conf.getDMRNetwork1Enabled()) {
		ret = createDMRNetwork1();
		if (!ret)
			return 1;
	}

	if (m_conf.getDMRNetwork2Enabled()) {
		ret = createDMRNetwork2();
		if (!ret)
			return 1;
	}

	if (m_conf.getDMRNetwork3Enabled()) {
		ret = createDMRNetwork3();
		if (!ret)
			return 1;
	}

	if (m_conf.getDMRNetwork4Enabled()) {
		ret = createDMRNetwork4();
		if (!ret)
			return 1;
	}

	if (m_conf.getDMRNetwork5Enabled()) {
		ret = createDMRNetwork5();
		if (!ret)
			return 1;
	}
	if (m_conf.getDMRNetwork6Enabled()) {
		ret = createDMRNetwork6();
		if (!ret)
			return 1;
	}

	if (m_conf.getDynamicTGControlEnabled()) {
		bool ret = createDynamicTGControl();
		if (!ret)
			return 1;
	}

	unsigned int rfTimeout  = m_conf.getRFTimeout();

	unsigned int netTimeout = m_conf.getNetTimeout();

	CTimer* timer[3U];
	timer[1U] = new CTimer(1000U);
	timer[2U] = new CTimer(1000U);

	unsigned int rfSrcId[3U];
	unsigned int rfDstId[3U];
	rfSrcId[1U] = rfSrcId[2U] = rfDstId[1U] = rfDstId[2U] = 0U;

	unsigned int dmr1SrcId[3U];
	unsigned int dmr1DstId[3U];
	dmr1SrcId[1U] = dmr1SrcId[2U] = dmr1DstId[1U] = dmr1DstId[2U] = 0U;

	unsigned int dmr2SrcId[3U];
	unsigned int dmr2DstId[3U];
	dmr2SrcId[1U] = dmr2SrcId[2U] = dmr2DstId[1U] = dmr2DstId[2U] = 0U;

	unsigned int dmr3SrcId[3U];
	unsigned int dmr3DstId[3U];
	dmr3SrcId[1U] = dmr3SrcId[2U] = dmr3DstId[1U] = dmr3DstId[2U] = 0U;

	unsigned int dmr4SrcId[3U];
	unsigned int dmr4DstId[3U];
	dmr4SrcId[1U] = dmr4SrcId[2U] = dmr4DstId[1U] = dmr4DstId[2U] = 0U;

	unsigned int dmr5SrcId[3U];
	unsigned int dmr5DstId[3U];
	dmr5SrcId[1U] = dmr5SrcId[2U] = dmr5DstId[1U] = dmr5DstId[2U] = 0U;

	unsigned int dmr6SrcId[3U];
	unsigned int dmr6DstId[3U];
	dmr6SrcId[1U] = dmr6SrcId[2U] = dmr6DstId[1U] = dmr6DstId[2U] = 0U;

	CStopWatch stopWatch;
	stopWatch.start();

	LogMessage("DMRGateway-%s is running", VERSION);

	while (!m_killed) {

		CDMRData data;

		bool ret = m_repeater->read(data);
		if (ret) {

			unsigned int slotNo = data.getSlotNo();
			unsigned int srcId = data.getSrcId();
			unsigned int dstId = data.getDstId();
			FLCO flco = data.getFLCO();


				bool trace = true;
				if (ruleTrace && (srcId != rfSrcId[slotNo] || dstId != rfDstId[slotNo])) {
					rfSrcId[slotNo] = srcId;
					rfDstId[slotNo] = dstId;
					trace = true;
				}

				if (trace)
					LogDebug("RF transmission RX: Net=%u, Slot=%u Src=%u Dst=%s%u", selnet, slotNo, srcId, flco == FLCO_GROUP ? "TG" : "", dstId);

				PROCESS_RESULT result = RESULT_UNMATCHED;

                                ctrlCode=0;

                       		if ( dstId > 9000 && dstId < 9007 ){
					ClearNetworks(); 
					storedtg = dstId;
                                	ctrlCode=1;
                                	if ( trace ) LogInfo("TESTAA Network keyed: %d", dstId);
                                	selnet = dstId-9000;
					if (trace) LogInfo("Selected 9000x Network = %d",selnet);
  					locknet = selnet;
					if (trace) LogInfo("Network Locked = %d",selnet);
                        	}

                       		if ( dstId > 999999 && dstId != storedtg) {					
					storedtg = dstId;
					ClearNetworks();
                                	if ( trace ) LogInfo("Radio TG Keyed = %d",dstId);
                                	ctrlCode=0;
                                	mult = ( dstId / 1000000 );
                                	selnet = mult;
					locknet=selnet;
                                	if (trace ) LogInfo("Calculated Network = %d",mult);
                                	LogDebug("Calculated TG = %d",dstId);
					LogInfo("Selected 7x Network = %d",selnet);
		                 }

				
				if ( rdstId != dstId ) {
					rdstId = dstId;
				}
				
				if ( trace ) LogInfo(" Active Network %d", selnet);

				 switch( selnet ) {
        					case 1 : if ( m_dmrNetwork1 != NULL )  net1ok=true;
							break;
        					case 2 : if ( m_dmrNetwork2 != NULL )  net2ok=true;
							break;
        					case 3 : if ( m_dmrNetwork3 != NULL )  net3ok=true;
							break;
        					case 4 : if ( m_dmrNetwork4 != NULL )  net4ok=true;
							break;
        					case 5 : if ( m_dmrNetwork5 != NULL )  net5ok=true;
							break;
        					case 6 : if ( m_dmrNetwork6 != NULL )  net6ok=true;
							break;
				}
				
		
				if ( trace ) {

					LogInfo("RF transmission: Net=%u, Slot=%u Src=%u Dst=%s%u", selnet, slotNo, srcId, flco == FLCO_GROUP ? "TG" : "", dstId);
				}

				if ( trace ) {
					LogInfo("Net1OK:%s    Net2OK:%s   Net3OK:%s", net1ok ? "yes" : "no", net2ok ? "yes" : "no", net3ok ? "yes" : "no" );
					LogInfo("Net4OK:%s    Net5OK:%s   Net6OK:%s", net4ok ? "yes" : "no", net5ok ? "yes" : "no", net6ok ? "yes" : "no" );
					LogInfo("Network Locked = %d", selnet);
				}


				if (m_dmrNetwork1 != NULL && net1ok) {

					// Rewrite the slot and/or TG or neither
					for (std::vector<CRewrite*>::iterator it = m_dmr1RFRewrites.begin(); it != m_dmr1RFRewrites.end(); ++it) {
						PROCESS_RESULT res = (*it)->process(data, trace);
						if (res != RESULT_UNMATCHED) {
							result = res;
							break;
						}
					}

					if (result == RESULT_MATCHED && net1ok && ctrlCode == 0) {
							if (trace) LogInfo("Network 1 Matched %d & Net1OK %s",selnet,net1ok ? "yes" : "no");

						if (m_status[slotNo] == DMRGWS_NONE || m_status[slotNo] == DMRGWS_DMRNETWORK1) {
							rewrite(m_dmr1SrcRewrites, data, trace);
							m_dmrNetwork1->write(data);
							m_status[slotNo] = DMRGWS_DMRNETWORK1;
							timer[slotNo]->setTimeout(rfTimeout);
							if ( trace ) LogInfo("Data Sent to Network 1");
							timer[slotNo]->start();
							if ( trace ) LogDebug("RF RX: Name=%s Net=%u Slot=%u Src=%u Dst=%s%u", m_dmr1Name.c_str(), selnet, slotNo, srcId, flco == FLCO_GROUP ? "TG" : "", dstId);
						}
					}
				}

				if (result == RESULT_UNMATCHED && net2ok) {
					if (m_dmrNetwork2 != NULL) {
						// Rewrite the slot and/or TG or neither
						for (std::vector<CRewrite*>::iterator it = m_dmr2RFRewrites.begin(); it != m_dmr2RFRewrites.end(); ++it) {
							PROCESS_RESULT res = (*it)->process(data, trace);
							if (res != RESULT_UNMATCHED) {
								result = res;
								break;
							}
						}

						if (result == RESULT_MATCHED && net2ok && ctrlCode ==0) {
							if (trace) LogInfo("Network 2 Matched %d & Net2OK %s",selnet,net2ok  ? "yes" : "no");

							if (m_status[slotNo] == DMRGWS_NONE || m_status[slotNo] == DMRGWS_DMRNETWORK2) {
								rewrite(m_dmr2SrcRewrites, data, trace);
								m_dmrNetwork2->write(data);
								m_status[slotNo] = DMRGWS_DMRNETWORK2;
								timer[slotNo]->setTimeout(rfTimeout);
								timer[slotNo]->start();
					if ( trace ) LogInfo("RF RX: Name=%s Net=%u Slot=%u Src=%u Dst=%s%u", m_dmr2Name.c_str(), selnet, slotNo, srcId, flco == FLCO_GROUP ? "TG" : "", dstId);

							}
						}
					}
				}

				if (result == RESULT_UNMATCHED && net3ok) {
					if (m_dmrNetwork3 != NULL) {
						// Rewrite the slot and/or TG or neither

						for (std::vector<CRewrite*>::iterator it = m_dmr3RFRewrites.begin(); it != m_dmr3RFRewrites.end(); ++it) {
							PROCESS_RESULT res = (*it)->process(data, trace);
							if (res != RESULT_UNMATCHED) {
								result = res;
								break;
							}
						}

						if (result == RESULT_MATCHED && net3ok && ctrlCode == 0) {
							if (trace) LogInfo("Network 3 Matched %d & Net3OK %s",selnet,net3ok ? "yes" : "no");

							if (m_status[slotNo] == DMRGWS_NONE || m_status[slotNo] == DMRGWS_DMRNETWORK3) {
								rewrite(m_dmr3SrcRewrites, data, trace);
								m_dmrNetwork3->write(data);
								m_status[slotNo] = DMRGWS_DMRNETWORK3;
								timer[slotNo]->setTimeout(rfTimeout);
								timer[slotNo]->start();
							if ( trace ) LogInfo("Data Sent to Network 3");
					if ( trace ) LogInfo("RF RX: Name=%s Net=%u Slot=%u Src=%u Dst=%s%u", m_dmr3Name.c_str(), selnet, slotNo, srcId, flco == FLCO_GROUP ? "TG" : "", dstId);
							}
						}
					}
				}
///////////////////////////
				if (result == RESULT_UNMATCHED && net4ok) {
					if (m_dmrNetwork4 != NULL) {
						// Rewrite the slot and/or TG or neither
						for (std::vector<CRewrite*>::iterator it = m_dmr4RFRewrites.begin(); it != m_dmr4RFRewrites.end(); ++it) {
							PROCESS_RESULT res = (*it)->process(data, trace);
							if (res != RESULT_UNMATCHED) {
								result = res;
								break;
							}
						}

						if (result == RESULT_MATCHED && net4ok && ctrlCode == 0) {
							if (trace) LogInfo("Network 4 Matched %d & Net4OK %s",selnet,net4ok ? "yes" : "no");
							if (m_status[slotNo] == DMRGWS_NONE || m_status[slotNo] == DMRGWS_DMRNETWORK4) {
								rewrite(m_dmr4SrcRewrites, data, trace);
								m_dmrNetwork4->write(data);
								m_status[slotNo] = DMRGWS_DMRNETWORK4;
								timer[slotNo]->setTimeout(rfTimeout);
								timer[slotNo]->start();
					if ( trace ) LogInfo("RF RX: Name=%s Net=%u Slot=%u Src=%u Dst=%s%u", m_dmr4Name.c_str(), selnet, slotNo, srcId, flco == FLCO_GROUP ? "TG" : "", dstId);
							}
						}
					}
				}
////////////////////////////
				if (result == RESULT_UNMATCHED && net5ok) {
					if (m_dmrNetwork5 != NULL) {
						// Rewrite the slot and/or TG or neither
						for (std::vector<CRewrite*>::iterator it = m_dmr5RFRewrites.begin(); it != m_dmr5RFRewrites.end(); ++it) {
							PROCESS_RESULT res = (*it)->process(data, trace);
							if (res != RESULT_UNMATCHED) {
								result = res;
								break;
							}
						}

						if (result == RESULT_MATCHED && net5ok && ctrlCode == 0) {
							if (trace) LogInfo("Network 5 Matched %d & Net5OK %s",selnet,net5ok ? "yes" : "no");
							if (m_status[slotNo] == DMRGWS_NONE || m_status[slotNo] == DMRGWS_DMRNETWORK5) {
								rewrite(m_dmr5SrcRewrites, data, trace);
								m_dmrNetwork5->write(data);
								m_status[slotNo] = DMRGWS_DMRNETWORK5;
								timer[slotNo]->setTimeout(rfTimeout);
								timer[slotNo]->start();
					if ( trace ) LogInfo("RF RX: Name=%s Net=%u Slot=%u Src=%u Dst=%s%u", m_dmr5Name.c_str(), selnet, slotNo, srcId, flco == FLCO_GROUP ? "TG" : "", dstId);
							}
						}
					}
				}

				if (result == RESULT_UNMATCHED && net6ok) {
					if (m_dmrNetwork6 != NULL) {
						// Rewrite the slot and/or TG or neither

						for (std::vector<CRewrite*>::iterator it = m_dmr6RFRewrites.begin(); it != m_dmr6RFRewrites.end(); ++it) {
							PROCESS_RESULT res = (*it)->process(data, trace);
							if (res != RESULT_UNMATCHED) {
								result = res;
								break;
							}
						}

						if (result == RESULT_MATCHED && net6ok && ctrlCode == 0) {
							if (trace) LogInfo("Network 6 Matched %d & Net6OK %s",selnet,net6ok ? "yes" : "no");
//	LogInfo("Net6OK %s", net6ok ? "yes" : "no");

							if (m_status[slotNo] == DMRGWS_NONE || m_status[slotNo] == DMRGWS_DMRNETWORK6) {
								rewrite(m_dmr6SrcRewrites, data, trace);
								m_dmrNetwork6->write(data);
								m_status[slotNo] = DMRGWS_DMRNETWORK6;
								timer[slotNo]->setTimeout(rfTimeout);
								timer[slotNo]->start();
					if ( trace ) LogInfo("RF RX: Name=%s Net=%u Slot=%u Src=%u Dst=%s%u", m_dmr6Name.c_str(), selnet, slotNo, srcId, flco == FLCO_GROUP ? "TG" : "", dstId);
							}
						}
					}
				}
				if (result == RESULT_UNMATCHED && net1ok) {
					if (m_dmrNetwork1 != NULL) {

						for (std::vector<CRewrite*>::iterator it = m_dmr1Passalls.begin(); it != m_dmr1Passalls.end(); ++it) {
							PROCESS_RESULT res = (*it)->process(data, trace);
							if (res != RESULT_UNMATCHED) {
								result = res;
								break;
							}
						}

						if (result == RESULT_MATCHED && net1ok && ctrlCode == 0) {
							if (trace) LogInfo("Network 1 Matched PassAll %d & Net1OK %s",selnet,net2ok  ? "yes" : "no");
							if (m_status[slotNo] == DMRGWS_NONE || m_status[slotNo] == DMRGWS_DMRNETWORK1) {
								rewrite(m_dmr1SrcRewrites, data, trace);
								m_dmrNetwork1->write(data);
								m_status[slotNo] = DMRGWS_DMRNETWORK1;
								timer[slotNo]->setTimeout(rfTimeout);
								timer[slotNo]->start();
							if (trace ) LogInfo("RF RX PassAll: Name=%s Net=%u Slot=%u Src=%u Dst=%s%u", m_dmr1Name.c_str(), selnet, slotNo, srcId, flco == FLCO_GROUP ? "TG" : "", dstId);
							}
						}
					}
				}

				if (result == RESULT_UNMATCHED && net2ok) {
					if (m_dmrNetwork2 != NULL) {

						for (std::vector<CRewrite*>::iterator it = m_dmr2Passalls.begin(); it != m_dmr2Passalls.end(); ++it) {
							PROCESS_RESULT res = (*it)->process(data, trace);
							if (res != RESULT_UNMATCHED) {
								result = res;
								break;
							}
						}

						if (result == RESULT_MATCHED && net2ok && ctrlCode == 0) {
							if (trace) LogInfo("Network 2 Matched PassAll %d & Net2OK %s",selnet,net2ok  ? "yes" : "no");

							if (m_status[slotNo] == DMRGWS_NONE || m_status[slotNo] == DMRGWS_DMRNETWORK2) {
								rewrite(m_dmr2SrcRewrites, data, trace);
								m_dmrNetwork2->write(data);
								m_status[slotNo] = DMRGWS_DMRNETWORK2;
								timer[slotNo]->setTimeout(rfTimeout);
								timer[slotNo]->start();
					if (trace ) LogInfo("RF RX PassAll: Name=%s Net=%u Slot=%u Src=%u Dst=%s%u", m_dmr2Name.c_str(), selnet,  slotNo, srcId, flco == FLCO_GROUP ? "TG" : "", dstId);
							}
						}
					}
				}

				if (result == RESULT_UNMATCHED && net3ok) {
					if (m_dmrNetwork3 != NULL) {

						for (std::vector<CRewrite*>::iterator it = m_dmr3Passalls.begin(); it != m_dmr3Passalls.end(); ++it) {
							PROCESS_RESULT res = (*it)->process(data, trace);
							if (res != RESULT_UNMATCHED) {
								result = res;
								break;
							}
						}

						if (result == RESULT_MATCHED && net3ok && ctrlCode == 0) {
							if (trace) LogInfo("Network 3 Matched PassAll %d & Net3OK %s",selnet,net3ok  ? "yes" : "no");

							if (m_status[slotNo] == DMRGWS_NONE || m_status[slotNo] == DMRGWS_DMRNETWORK3) {
								rewrite(m_dmr3SrcRewrites, data, trace);
								m_dmrNetwork3->write(data);
								m_status[slotNo] = DMRGWS_DMRNETWORK3;
								timer[slotNo]->setTimeout(rfTimeout);
								timer[slotNo]->start();
					if ( trace) LogInfo("RF RX PassAll: Name=%s Net=%u Slot=%u Src=%u Dst=%s%u", m_dmr3Name.c_str(), selnet, slotNo, srcId, flco == FLCO_GROUP ? "TG" : "", dstId);
							}
						}
					}
				}

				if (result == RESULT_UNMATCHED && net4ok) {
					if (m_dmrNetwork4 != NULL) {

						for (std::vector<CRewrite*>::iterator it = m_dmr4Passalls.begin(); it != m_dmr4Passalls.end(); ++it) {
							PROCESS_RESULT res = (*it)->process(data, trace);
							if (res != RESULT_UNMATCHED) {
								result = res;
								break;
							}
						}

						if (result == RESULT_MATCHED && net4ok && ctrlCode == 0) {
							if (trace) LogInfo("Network 4 Matched PassAll %d & Net4OK %s",selnet,net4ok  ? "yes" : "no");

							if (m_status[slotNo] == DMRGWS_NONE || m_status[slotNo] == DMRGWS_DMRNETWORK4) {
								rewrite(m_dmr4SrcRewrites, data, trace);
								m_dmrNetwork4->write(data);
								m_status[slotNo] = DMRGWS_DMRNETWORK4;
								timer[slotNo]->setTimeout(rfTimeout);
								timer[slotNo]->start();
					if ( trace ) LogInfo("RF RX PassAll: Name=%s Net=%s Slot=%u Src=%u Dst=%s%u", m_dmr4Name.c_str(), selnet, slotNo, srcId, flco == FLCO_GROUP ? "TG" : "", dstId);

							}
						}
					}
				}

				if (result == RESULT_UNMATCHED && net5ok) {
					if (m_dmrNetwork5 != NULL) {

						for (std::vector<CRewrite*>::iterator it = m_dmr5Passalls.begin(); it != m_dmr5Passalls.end(); ++it) {
							PROCESS_RESULT res = (*it)->process(data, trace);
							if (res != RESULT_UNMATCHED) {
								result = res;
								break;
							}
						}

						if (result == RESULT_MATCHED && net5ok && ctrlCode == 0) {
							if (trace) LogInfo("Network 5 Matched PassAll %d & Net5OK %s",selnet,net5ok  ? "yes" : "no");

							if (m_status[slotNo] == DMRGWS_NONE || m_status[slotNo] == DMRGWS_DMRNETWORK5) {
								rewrite(m_dmr5SrcRewrites, data, trace);
								m_dmrNetwork5->write(data);
								m_status[slotNo] = DMRGWS_DMRNETWORK5;
								timer[slotNo]->setTimeout(rfTimeout);
								timer[slotNo]->start();
							if ( trace ) LogInfo("RF RX PassAll: Name=%s Net=%u Slot=%u Src=%u Dst=%s%u", m_dmr5Name.c_str(), selnet, slotNo, srcId, flco == FLCO_GROUP ? "TG" : "", dstId);
					}
						}
					}
				}

				if (result == RESULT_UNMATCHED && net6ok) {
					if (m_dmrNetwork6 != NULL) {
						if (net6ok == false) LogInfo("Net6OK = False");
						for (std::vector<CRewrite*>::iterator it = m_dmr6Passalls.begin(); it != m_dmr6Passalls.end(); ++it) {
							PROCESS_RESULT res = (*it)->process(data, trace);
							if (res != RESULT_UNMATCHED) {
								result = res;
								break;
							}
						}

						if (result == RESULT_MATCHED && net6ok && ctrlCode == 0) {
							if (trace) LogInfo("Network 6 Matched PassAll %d & Net6OK %s",selnet,net6ok  ? "yes" : "no");

							if (m_status[slotNo] == DMRGWS_NONE || m_status[slotNo] == DMRGWS_DMRNETWORK6) {
								rewrite(m_dmr6SrcRewrites, data, trace);
								m_dmrNetwork6->write(data);
								m_status[slotNo] = DMRGWS_DMRNETWORK6;
								timer[slotNo]->setTimeout(rfTimeout);
								timer[slotNo]->start();
					if ( trace ) LogInfo("RF RX PassAll:: Name=%s Net=%u Slot=%u Src=%u Dst=%s%u", m_dmr6Name.c_str(), selnet, slotNo, srcId, flco == FLCO_GROUP ? "TG" : "", dstId);
							}
						}
					}
				}



				if (result == RESULT_UNMATCHED && trace)
					LogInfo("Rule Trace,\tnot matched so rejected");
			}
		


// Start of Network Receive Data ////////////////////////////

		if (m_dmrNetwork1 != NULL) {
			ret = m_dmrNetwork1->read(data);
			if (ret && net1ok) {
				unsigned int slotNo = data.getSlotNo();
				unsigned int srcId  = data.getSrcId();
				unsigned int dstId  = data.getDstId();
				FLCO flco           = data.getFLCO();

				bool trace = false;
				if (ruleTrace && (srcId != dmr1SrcId[slotNo] || dstId != dmr1DstId[slotNo])) {
					dmr1SrcId[slotNo] = srcId;
					dmr1DstId[slotNo] = dstId;
					trace = true;
					LogInfo("Network 1 RX Data: Name=%s Slot=%u Src=%u Dst=%s%u", m_dmr1Name.c_str(), slotNo, srcId, flco == FLCO_GROUP ? "TG" : "", dstId);

				}


				// Rewrite the slot and/or TG or neither
				bool rewritten = false;
				for (std::vector<CRewrite*>::iterator it = m_dmr1NetRewrites.begin(); it != m_dmr1NetRewrites.end(); ++it) {
					bool ret = (*it)->process(data, trace);
					if (ret) {
						rewritten = true;
						break;
					}
				}

				if (rewritten) {
					// Check that the rewritten slot is free to use.
					slotNo = data.getSlotNo();
					if (m_status[slotNo] == DMRGWS_NONE || m_status[slotNo] == DMRGWS_DMRNETWORK1) {
						for (std::vector<CRewriteDynTGRF*>::iterator it = m_dynRF.begin(); it != m_dynRF.end(); ++it)
							(*it)->stopVoice(slotNo);
						m_repeater->write(data);
						m_status[slotNo] = DMRGWS_DMRNETWORK1;
						timer[slotNo]->setTimeout(netTimeout);
						timer[slotNo]->start();
					}
				}

				if (!rewritten && trace)
					LogDebug("Rule Trace,\tnot matched so rejected");
			}

			ret = m_dmrNetwork1->wantsBeacon();
			if (ret)
				m_repeater->writeBeacon();
		}

		if (m_dmrNetwork2 != NULL) {
			ret = m_dmrNetwork2->read(data);
			if (ret && net2ok) {
				unsigned int slotNo = data.getSlotNo();
				unsigned int srcId  = data.getSrcId();
				unsigned int dstId  = data.getDstId();
				FLCO flco           = data.getFLCO();

				bool trace = false;
				if (ruleTrace && (srcId != dmr2SrcId[slotNo] || dstId != dmr2DstId[slotNo])) {
					dmr2SrcId[slotNo] = srcId;
					dmr2DstId[slotNo] = dstId;
					trace = true;
					LogInfo("Network 2 RX Data: Name=%s Slot=%u Src=%u Dst=%s%u", m_dmr2Name.c_str(), slotNo, srcId, flco == FLCO_GROUP ? "TG" : "", dstId);
				}


				// Rewrite the slot and/or TG or neither
				bool rewritten = false;
				for (std::vector<CRewrite*>::iterator it = m_dmr2NetRewrites.begin(); it != m_dmr2NetRewrites.end(); ++it) {
					bool ret = (*it)->process(data, trace);
					if (ret) {
						rewritten = true;
						break;
					}
				}

				if (rewritten) {
					// Check that the rewritten slot is free to use.
					slotNo = data.getSlotNo();
					if (m_status[slotNo] == DMRGWS_NONE || m_status[slotNo] == DMRGWS_DMRNETWORK2) {
						for (std::vector<CRewriteDynTGRF*>::iterator it = m_dynRF.begin(); it != m_dynRF.end(); ++it)
							(*it)->stopVoice(slotNo);
						m_repeater->write(data);
						m_status[slotNo] = DMRGWS_DMRNETWORK2;
						timer[slotNo]->setTimeout(netTimeout);
						timer[slotNo]->start();
					}
				}

				if (!rewritten && trace)
					LogDebug("Rule Trace,\tnot matched so rejected");
			}

			ret = m_dmrNetwork2->wantsBeacon();
			if (ret && net2ok)
				m_repeater->writeBeacon();
		}

		if (m_dmrNetwork3 != NULL) {
			ret = m_dmrNetwork3->read(data);
			if (ret && net3ok) {
				unsigned int slotNo = data.getSlotNo();
				unsigned int srcId = data.getSrcId();
				unsigned int dstId = data.getDstId();
				FLCO flco = data.getFLCO();

				bool trace = false;
				if (ruleTrace && (srcId != dmr3SrcId[slotNo] || dstId != dmr3DstId[slotNo])) {
					dmr3SrcId[slotNo] = srcId;
					dmr3DstId[slotNo] = dstId;
					trace = true;
					LogInfo("Network 3 RX Data: Name=%s Slot=%u Src=%u Dst=%s%u", m_dmr3Name.c_str(), slotNo, srcId, flco == FLCO_GROUP ? "TG" : "", dstId);
				}


				// Rewrite the slot and/or TG or neither
				bool rewritten = false;
				for (std::vector<CRewrite*>::iterator it = m_dmr3NetRewrites.begin(); it != m_dmr3NetRewrites.end(); ++it) {
					bool ret = (*it)->process(data, trace);
					if (ret) {
						rewritten = true;
						break;
					}
				}

				if (rewritten) {
					// Check that the rewritten slot is free to use.
					slotNo = data.getSlotNo();
					if (m_status[slotNo] == DMRGWS_NONE || m_status[slotNo] == DMRGWS_DMRNETWORK3) {
						for (std::vector<CRewriteDynTGRF*>::iterator it = m_dynRF.begin(); it != m_dynRF.end(); ++it)
							(*it)->stopVoice(slotNo);
						m_repeater->write(data);
						m_status[slotNo] = DMRGWS_DMRNETWORK3;
						timer[slotNo]->setTimeout(netTimeout);
						timer[slotNo]->start();
					}
				}

				if (!rewritten && trace)
					LogDebug("Rule Trace,\tnot matched so rejected");
			}

			ret = m_dmrNetwork3->wantsBeacon();
			if (ret)
				m_repeater->writeBeacon();
		}
/////////////////////////////

		if (m_dmrNetwork4 != NULL) {
			ret = m_dmrNetwork4->read(data);
			if (ret && net4ok ) {
				unsigned int slotNo = data.getSlotNo();
				unsigned int srcId = data.getSrcId();
				unsigned int dstId = data.getDstId();
				FLCO flco = data.getFLCO();

				bool trace = false;
				if (ruleTrace && (srcId != dmr4SrcId[slotNo] || dstId != dmr4DstId[slotNo])) {
					dmr4SrcId[slotNo] = srcId;
					dmr4DstId[slotNo] = dstId;
					trace = true;
					LogInfo("Network 4 RX Data: Name=%s Slot=%u Src=%u Dst=%s%u", m_dmr4Name.c_str(), slotNo, srcId, flco == FLCO_GROUP ? "TG" : "", dstId);
				}

				// Rewrite the slot and/or TG or neither
				bool rewritten = false;
				for (std::vector<CRewrite*>::iterator it = m_dmr4NetRewrites.begin(); it != m_dmr4NetRewrites.end(); ++it) {
					bool ret = (*it)->process(data, trace);
//					if (ret && dstId == rdstId) {
					if (ret) {
						rewritten = true;
						break;
					}
				}

				if (rewritten) {
					// Check that the rewritten slot is free to use.
					slotNo = data.getSlotNo();
					if (m_status[slotNo] == DMRGWS_NONE || m_status[slotNo] == DMRGWS_DMRNETWORK4) {
						for (std::vector<CRewriteDynTGRF*>::iterator it = m_dynRF.begin(); it != m_dynRF.end(); ++it)
							(*it)->stopVoice(slotNo);
						m_repeater->write(data);
						m_status[slotNo] = DMRGWS_DMRNETWORK4;
						timer[slotNo]->setTimeout(netTimeout);
						timer[slotNo]->start();
	//			if (trace)
	//				LogInfo("Rule Trace, Network 4 Transmission: Slot=%u Src=%u Dst=%s%u", slotNo, srcId, flco == FLCO_GROUP ? "TG" : "", dstId);

					}
				}

				if (!rewritten && trace)
					LogDebug("Rule Trace,\tnot matched so rejected");
			}

			ret = m_dmrNetwork4->wantsBeacon();
			if (ret)
				m_repeater->writeBeacon();
		}
/////////////

		if (m_dmrNetwork5 != NULL) {
			ret = m_dmrNetwork5->read(data);
			if (ret && net5ok) {
				unsigned int slotNo = data.getSlotNo();
				unsigned int srcId = data.getSrcId();
				unsigned int dstId = data.getDstId();
				FLCO flco = data.getFLCO();

				bool trace = false;
				if (ruleTrace && (srcId != dmr5SrcId[slotNo] || dstId != dmr5DstId[slotNo])) {
					dmr5SrcId[slotNo] = srcId;
					dmr5DstId[slotNo] = dstId;
					trace = true;
					LogInfo("Network 5 RX Data: Name=%s Slot=%u Src=%u Dst=%s%u", m_dmr5Name.c_str(), slotNo, srcId, flco == FLCO_GROUP ? "TG" : "", dstId);
				}


				// Rewrite the slot and/or TG or neither
				bool rewritten = false;
				for (std::vector<CRewrite*>::iterator it = m_dmr5NetRewrites.begin(); it != m_dmr5NetRewrites.end(); ++it) {
					bool ret = (*it)->process(data, trace);
//					if (ret && dstId == rdstId) {
					if (ret) {
						rewritten = true;
						break;
					}
				}

				if (rewritten) {
					// Check that the rewritten slot is free to use.
					slotNo = data.getSlotNo();
					if (m_status[slotNo] == DMRGWS_NONE || m_status[slotNo] == DMRGWS_DMRNETWORK5) {
						for (std::vector<CRewriteDynTGRF*>::iterator it = m_dynRF.begin(); it != m_dynRF.end(); ++it)
							(*it)->stopVoice(slotNo);
						m_repeater->write(data);
						m_status[slotNo] = DMRGWS_DMRNETWORK5;
						timer[slotNo]->setTimeout(netTimeout);
						timer[slotNo]->start();
					}
				}

				if (!rewritten && trace)
					LogDebug("Rule Trace,\tnot matched so rejected");
			}

			ret = m_dmrNetwork5->wantsBeacon();
			if (ret)
				m_repeater->writeBeacon();
		}


		if (m_dmrNetwork6 != NULL) {
			ret = m_dmrNetwork6->read(data);
			if (ret && net6ok) {
				unsigned int slotNo = data.getSlotNo();
				unsigned int srcId = data.getSrcId();
				unsigned int dstId = data.getDstId();
				FLCO flco = data.getFLCO();

				bool trace = false;
				if (ruleTrace && (srcId != dmr6SrcId[slotNo] || dstId != dmr6DstId[slotNo])) {
					dmr6SrcId[slotNo] = srcId;
					dmr6DstId[slotNo] = dstId;
					trace = true;
					LogInfo("Network 6 RX Data: Name=%s Slot=%u Src=%u Dst=%s%u", m_dmr6Name.c_str(), slotNo, srcId, flco == FLCO_GROUP ? "TG" : "", dstId);
				}


				// Rewrite the slot and/or TG or neither
				bool rewritten = false;
				for (std::vector<CRewrite*>::iterator it = m_dmr6NetRewrites.begin(); it != m_dmr6NetRewrites.end(); ++it) {
					bool ret = (*it)->process(data, trace);
					if (ret) {
						rewritten = true;
						break;
					}
				}

				if (rewritten) {
					// Check that the rewritten slot is free to use.
					slotNo = data.getSlotNo();
					if (m_status[slotNo] == DMRGWS_NONE || m_status[slotNo] == DMRGWS_DMRNETWORK6) {
						for (std::vector<CRewriteDynTGRF*>::iterator it = m_dynRF.begin(); it != m_dynRF.end(); ++it)
							(*it)->stopVoice(slotNo);
						m_repeater->write(data);
						m_status[slotNo] = DMRGWS_DMRNETWORK6;
						timer[slotNo]->setTimeout(netTimeout);
						timer[slotNo]->start();
					}
				}

				if (!rewritten && trace)
					LogDebug("Rule Trace,\tnot matched so rejected");
			}

			ret = m_dmrNetwork6->wantsBeacon();
			if (ret)
				m_repeater->writeBeacon();
		}



		processRadioPosition();

		processTalkerAlias();

		processHomePosition();


		if (m_socket != NULL)
			processDynamicTGControl();

		unsigned int ms = stopWatch.elapsed();
		stopWatch.start();

		m_repeater->clock(ms);

		m_xlxRelink.clock(ms);

		if (m_dmrNetwork1 != NULL)
			m_dmrNetwork1->clock(ms);

		if (m_dmrNetwork2 != NULL)
			m_dmrNetwork2->clock(ms);

		if (m_dmrNetwork3 != NULL)
			m_dmrNetwork3->clock(ms);

		if (m_dmrNetwork4 != NULL)
			m_dmrNetwork4->clock(ms);

		if (m_dmrNetwork5 != NULL)
			m_dmrNetwork5->clock(ms);

		if (m_dmrNetwork6 != NULL)
			m_dmrNetwork6->clock(ms);

		for (std::vector<CDynVoice*>::iterator it = m_dynVoices.begin(); it != m_dynVoices.end(); ++it)
			(*it)->clock(ms);

		for (unsigned int i = 1U; i < 3U; i++) {
			timer[i]->clock(ms);
			if (timer[i]->isRunning() && timer[i]->hasExpired()) {
				m_status[i] = DMRGWS_NONE;
				timer[i]->stop();
			}
		}

		if (ms < 10U)
			CThread::sleep(10U);
	}


	m_repeater->close();
	delete m_repeater;

	if (m_dmrNetwork1 != NULL) {
		m_dmrNetwork1->close();
		delete m_dmrNetwork1;
	}

	if (m_dmrNetwork2 != NULL) {
		m_dmrNetwork2->close();
		delete m_dmrNetwork2;
	}

	if (m_dmrNetwork3 != NULL) {
		m_dmrNetwork3->close();
		delete m_dmrNetwork3;
	}

	if (m_dmrNetwork4 != NULL) {
		m_dmrNetwork4->close();
		delete m_dmrNetwork4;
	}

	if (m_dmrNetwork5 != NULL) {
		m_dmrNetwork5->close();
		delete m_dmrNetwork5;
	}

	if (m_dmrNetwork6 != NULL) {
		m_dmrNetwork6->close();
		delete m_dmrNetwork6;
	}

	if (m_socket != NULL) {
		m_socket->close();
		delete m_socket;
	}

	delete timer[1U];
	delete timer[2U];


	return 0;
}

bool CDMRGateway::createMMDVM()
{
	std::string rptAddress   = m_conf.getRptAddress();
	unsigned int rptPort     = m_conf.getRptPort();
	std::string localAddress = m_conf.getLocalAddress();
	unsigned int localPort   = m_conf.getLocalPort();
	bool debug               = m_conf.getDebug();

	LogInfo("MMDVM Network Parameters");
	LogInfo("    Rpt Address: %s", rptAddress.c_str());
	LogInfo("    Rpt Port: %u", rptPort);
	LogInfo("    Local Address: %s", localAddress.c_str());
	LogInfo("    Local Port: %u", localPort);

	m_repeater = new CMMDVMNetwork(rptAddress, rptPort, localAddress, localPort, debug);

	bool ret = m_repeater->open();
	if (!ret) {
		delete m_repeater;
		m_repeater = NULL;
		return false;
	}

	return true;
}

bool CDMRGateway::createDMRNetwork1()
{
	std::string address  = m_conf.getDMRNetwork1Address();
	unsigned int port    = m_conf.getDMRNetwork1Port();
	unsigned int local   = m_conf.getDMRNetwork1Local();
	unsigned int id      = m_conf.getDMRNetwork1Id();
	std::string password = m_conf.getDMRNetwork1Password();
	bool location        = m_conf.getDMRNetwork1Location();
	bool debug           = m_conf.getDMRNetwork1Debug();
	m_dmr1Name           = m_conf.getDMRNetwork1Name();

	if (id == 0U)
		id = m_repeater->getId();

	LogInfo("DMR Network 1 Parameters");
	LogInfo("    Name: %s", m_dmr1Name.c_str());
	LogInfo("    Id: %u", id);
	LogInfo("    Address: %s", address.c_str());
	LogInfo("    Port: %u", port);
	if (local > 0U)
		LogInfo("    Local: %u", local);
	else
		LogInfo("    Local: random");
	LogInfo("    Location Data: %s", location ? "yes" : "no");

	m_dmrNetwork1 = new CDMRNetwork(address, port, local, id, password, m_dmr1Name, VERSION, debug);

	std::string options = m_conf.getDMRNetwork1Options();
	if (options.empty())
		options = m_repeater->getOptions();

	if (!options.empty()) {
		LogInfo("    Options: %s", options.c_str());
		m_dmrNetwork1->setOptions(options);
	}

	unsigned char config[400U];
	unsigned int len = getConfig(m_dmr1Name, config);

	if (!location)
		::memcpy(config + 30U, "0.00000000.000000", 17U);

	m_dmrNetwork1->setConfig(config, len);

	bool ret = m_dmrNetwork1->open();
	if (!ret) {
		delete m_dmrNetwork1;
		m_dmrNetwork1 = NULL;
		return false;
	}

	std::vector<CTGRewriteStruct> tgRewrites = m_conf.getDMRNetwork1TGRewrites();
	for (std::vector<CTGRewriteStruct>::const_iterator it = tgRewrites.begin(); it != tgRewrites.end(); ++it) {
		if ((*it).m_range == 1)
			LogInfo("    Rewrite RF: %u:TG%u -> %u:TG%u", (*it).m_fromSlot, (*it).m_fromTG, (*it).m_toSlot, (*it).m_toTG);
		else
			LogInfo("    Rewrite RF: %u:TG%u-TG%u -> %u:TG%u-TG%u", (*it).m_fromSlot, (*it).m_fromTG, (*it).m_fromTG + (*it).m_range - 1U, (*it).m_toSlot, (*it).m_toTG, (*it).m_toTG + (*it).m_range - 1U);
		if ((*it).m_range == 1)
			LogInfo("    Rewrite Net: %u:TG%u -> %u:TG%u", (*it).m_toSlot, (*it).m_toTG, (*it).m_fromSlot, (*it).m_fromTG);
		else
			LogInfo("    Rewrite Net: %u:TG%u-TG%u -> %u:TG%u-TG%u", (*it).m_toSlot, (*it).m_toTG, (*it).m_toTG + (*it).m_range - 1U, (*it).m_fromSlot, (*it).m_fromTG, (*it).m_fromTG + (*it).m_range - 1U);

		CRewriteTG* rfRewrite  = new CRewriteTG(m_dmr1Name, (*it).m_fromSlot, (*it).m_fromTG, (*it).m_toSlot, (*it).m_toTG, (*it).m_range);
		CRewriteTG* netRewrite = new CRewriteTG(m_dmr1Name, (*it).m_toSlot, (*it).m_toTG, (*it).m_fromSlot, (*it).m_fromTG, (*it).m_range);

		m_dmr1RFRewrites.push_back(rfRewrite);
		m_dmr1NetRewrites.push_back(netRewrite);
	}

	std::vector<CPCRewriteStruct> pcRewrites = m_conf.getDMRNetwork1PCRewrites();
	for (std::vector<CPCRewriteStruct>::const_iterator it = pcRewrites.begin(); it != pcRewrites.end(); ++it) {
		if ((*it).m_range == 1)
			LogInfo("    Rewrite RF: %u:%u -> %u:%u", (*it).m_fromSlot, (*it).m_fromId, (*it).m_toSlot, (*it).m_toId);
		else
			LogInfo("    Rewrite RF: %u:%u-%u -> %u:%u-%u", (*it).m_fromSlot, (*it).m_fromId, (*it).m_fromId + (*it).m_range - 1U, (*it).m_toSlot, (*it).m_toId, (*it).m_toId + (*it).m_range - 1U);

		CRewritePC* rewrite = new CRewritePC(m_dmr1Name, (*it).m_fromSlot, (*it).m_fromId, (*it).m_toSlot, (*it).m_toId, (*it).m_range);

		m_dmr1RFRewrites.push_back(rewrite);
	}

	std::vector<CTypeRewriteStruct> typeRewrites = m_conf.getDMRNetwork1TypeRewrites();
	for (std::vector<CTypeRewriteStruct>::const_iterator it = typeRewrites.begin(); it != typeRewrites.end(); ++it) {
		if ((*it).m_range == 1)
			LogInfo("    Rewrite RF: %u:TG%u -> %u:%u", (*it).m_fromSlot, (*it).m_fromTG, (*it).m_toSlot, (*it).m_toId);
		else
			LogInfo("    Rewrite RF: %u:TG%u-%u -> %u:%u-%u", (*it).m_fromSlot, (*it).m_fromTG, (*it).m_fromTG + (*it).m_range - 1U, (*it).m_toSlot, (*it).m_toId, (*it).m_toId + (*it).m_range - 1U);

		CRewriteType* rewrite = new CRewriteType(m_dmr1Name, (*it).m_fromSlot, (*it).m_fromTG, (*it).m_toSlot, (*it).m_toId, (*it).m_range);

		m_dmr1RFRewrites.push_back(rewrite);
	}

	std::vector<CSrcRewriteStruct> srcRewrites = m_conf.getDMRNetwork1SrcRewrites();
	for (std::vector<CSrcRewriteStruct>::const_iterator it = srcRewrites.begin(); it != srcRewrites.end(); ++it) {
		if ((*it).m_range == 1)
			LogInfo("    Rewrite Net: %u:%u -> %u:TG%u", (*it).m_fromSlot, (*it).m_fromId, (*it).m_toSlot, (*it).m_toTG);
		else
			LogInfo("    Rewrite Net: %u:%u-%u -> %u:TG%u", (*it).m_fromSlot, (*it).m_fromId, (*it).m_fromId + (*it).m_range - 1U, (*it).m_toSlot, (*it).m_toTG);

		CRewriteSrc* rewrite = new CRewriteSrc(m_dmr1Name, (*it).m_fromSlot, (*it).m_fromId, (*it).m_toSlot, (*it).m_toTG, (*it).m_range);

		m_dmr1NetRewrites.push_back(rewrite);
	}

	std::vector<CTGDynRewriteStruct> dynRewrites = m_conf.getDMRNetwork1TGDynRewrites();
	for (std::vector<CTGDynRewriteStruct>::const_iterator it = dynRewrites.begin(); it != dynRewrites.end(); ++it) {
		LogInfo("    Dyn Rewrite: %u:TG%u-%u:TG%u <-> %u:TG%u (disc %u:%u) (status %u:%u) (%u exclusions)", (*it).m_slot, (*it).m_fromTG, (*it).m_slot, (*it).m_fromTG + (*it).m_range - 1U, (*it).m_slot, (*it).m_toTG, (*it).m_slot, (*it).m_discPC, (*it).m_slot, (*it).m_statusPC, (*it).m_exclTGs.size());

		CDynVoice* voice = NULL;
		if (m_conf.getVoiceEnabled()) {
			std::string language  = m_conf.getVoiceLanguage();
			std::string directory = m_conf.getVoiceDirectory();

			voice = new CDynVoice(directory, language, m_repeater->getId(), (*it).m_slot, (*it).m_toTG);
			bool ret = voice->open();
			if (!ret) {
				delete voice;
				voice = NULL;
			} else {
				m_dynVoices.push_back(voice);
			}
		}

		CRewriteDynTGNet* netRewriteDynTG = new CRewriteDynTGNet(m_dmr1Name, (*it).m_slot, (*it).m_toTG);
		CRewriteDynTGRF* rfRewriteDynTG = new CRewriteDynTGRF(m_dmr1Name, (*it).m_slot, (*it).m_fromTG, (*it).m_toTG, (*it).m_discPC, (*it).m_statusPC, (*it).m_range, (*it).m_exclTGs, netRewriteDynTG, voice);

		m_dmr1RFRewrites.push_back(rfRewriteDynTG);
		m_dmr1NetRewrites.push_back(netRewriteDynTG);
		m_dynRF.push_back(rfRewriteDynTG);
	}

	std::vector<CIdRewriteStruct> idRewrites = m_conf.getDMRNetwork1IdRewrites();
	for (std::vector<CIdRewriteStruct>::const_iterator it = idRewrites.begin(); it != idRewrites.end(); ++it) {
		LogInfo("    Rewrite Id: %u <-> %u", (*it).m_rfId, (*it).m_netId);

		CRewriteSrcId* rewriteSrcId = new CRewriteSrcId(m_dmr1Name, (*it).m_rfId, (*it).m_netId);
		CRewriteDstId* rewriteDstId = new CRewriteDstId(m_dmr1Name, (*it).m_netId, (*it).m_rfId);

		m_dmr1SrcRewrites.push_back(rewriteSrcId);
		m_dmr1NetRewrites.push_back(rewriteDstId);
	}

	std::vector<unsigned int> tgPassAll = m_conf.getDMRNetwork1PassAllTG();
	for (std::vector<unsigned int>::const_iterator it = tgPassAll.begin(); it != tgPassAll.end(); ++it) {
		LogInfo("    Pass All TG: %u", *it);

		CPassAllTG* rfPassAllTG  = new CPassAllTG(m_dmr1Name, *it);
		CPassAllTG* netPassAllTG = new CPassAllTG(m_dmr1Name, *it);

		m_dmr1Passalls.push_back(rfPassAllTG);
		m_dmr1NetRewrites.push_back(netPassAllTG);
	}

	std::vector<unsigned int> pcPassAll = m_conf.getDMRNetwork1PassAllPC();
	for (std::vector<unsigned int>::const_iterator it = pcPassAll.begin(); it != pcPassAll.end(); ++it) {
		LogInfo("    Pass All PC: %u", *it);

		CPassAllPC* rfPassAllPC  = new CPassAllPC(m_dmr1Name, *it);
		CPassAllPC* netPassAllPC = new CPassAllPC(m_dmr1Name, *it);

		m_dmr1Passalls.push_back(rfPassAllPC);
		m_dmr1NetRewrites.push_back(netPassAllPC);
	}

	return true;
}

bool CDMRGateway::createDMRNetwork2()
{
	std::string address  = m_conf.getDMRNetwork2Address();
	unsigned int port    = m_conf.getDMRNetwork2Port();
	unsigned int local   = m_conf.getDMRNetwork2Local();
	unsigned int id      = m_conf.getDMRNetwork2Id();
	std::string password = m_conf.getDMRNetwork2Password();
	bool location        = m_conf.getDMRNetwork2Location();
	bool debug           = m_conf.getDMRNetwork2Debug();
	m_dmr2Name           = m_conf.getDMRNetwork2Name();

	if (id == 0U)
		id = m_repeater->getId();

	LogInfo("DMR Network 2 Parameters");
	LogInfo("    Name: %s", m_dmr2Name.c_str());
	LogInfo("    Id: %u", id);
	LogInfo("    Address: %s", address.c_str());
	LogInfo("    Port: %u", port);
	if (local > 0U)
		LogInfo("    Local: %u", local);
	else
		LogInfo("    Local: random");
	LogInfo("    Location Data: %s", location ? "yes" : "no");

	m_dmrNetwork2 = new CDMRNetwork(address, port, local, id, password, m_dmr2Name, VERSION, debug);

	std::string options = m_conf.getDMRNetwork2Options();
	if (options.empty())
		options = m_repeater->getOptions();

	if (!options.empty()) {
		LogInfo("    Options: %s", options.c_str());
		m_dmrNetwork2->setOptions(options);
	}

	unsigned char config[400U];
	unsigned int len = getConfig(m_dmr2Name, config);

	if (!location)
		::memcpy(config + 30U, "0.00000000.000000", 17U);

	m_dmrNetwork2->setConfig(config, len);

	bool ret = m_dmrNetwork2->open();
	if (!ret) {
		delete m_dmrNetwork2;
		m_dmrNetwork2 = NULL;
		return false;
	}

	std::vector<CTGRewriteStruct> tgRewrites = m_conf.getDMRNetwork2TGRewrites();
	for (std::vector<CTGRewriteStruct>::const_iterator it = tgRewrites.begin(); it != tgRewrites.end(); ++it) {
		if ((*it).m_range == 1)
			LogInfo("    Rewrite RF: %u:TG%u -> %u:TG%u", (*it).m_fromSlot, (*it).m_fromTG, (*it).m_toSlot, (*it).m_toTG);
		else
			LogInfo("    Rewrite RF: %u:TG%u-TG%u -> %u:TG%u-TG%u", (*it).m_fromSlot, (*it).m_fromTG, (*it).m_fromTG + (*it).m_range - 1U, (*it).m_toSlot, (*it).m_toTG, (*it).m_toTG + (*it).m_range - 1U);
		if ((*it).m_range == 1)
			LogInfo("    Rewrite Net: %u:TG%u -> %u:TG%u", (*it).m_toSlot, (*it).m_toTG, (*it).m_fromSlot, (*it).m_fromTG);
		else
			LogInfo("    Rewrite Net: %u:TG%u-TG%u -> %u:TG%u-TG%u", (*it).m_toSlot, (*it).m_toTG, (*it).m_toTG + (*it).m_range - 1U, (*it).m_fromSlot, (*it).m_fromTG, (*it).m_fromTG + (*it).m_range - 1U);

		CRewriteTG* rfRewrite  = new CRewriteTG(m_dmr2Name, (*it).m_fromSlot, (*it).m_fromTG, (*it).m_toSlot, (*it).m_toTG, (*it).m_range);
		CRewriteTG* netRewrite = new CRewriteTG(m_dmr2Name, (*it).m_toSlot, (*it).m_toTG, (*it).m_fromSlot, (*it).m_fromTG, (*it).m_range);

		m_dmr2RFRewrites.push_back(rfRewrite);
		m_dmr2NetRewrites.push_back(netRewrite);
	}

	std::vector<CPCRewriteStruct> pcRewrites = m_conf.getDMRNetwork2PCRewrites();
	for (std::vector<CPCRewriteStruct>::const_iterator it = pcRewrites.begin(); it != pcRewrites.end(); ++it) {
		if ((*it).m_range == 1)
			LogInfo("    Rewrite RF: %u:%u -> %u:%u", (*it).m_fromSlot, (*it).m_fromId, (*it).m_toSlot, (*it).m_toId);
		else
			LogInfo("    Rewrite RF: %u:%u-%u -> %u:%u-%u", (*it).m_fromSlot, (*it).m_fromId, (*it).m_fromId + (*it).m_range - 1U, (*it).m_toSlot, (*it).m_toId, (*it).m_toId + (*it).m_range - 1U);

		CRewritePC* rewrite = new CRewritePC(m_dmr2Name, (*it).m_fromSlot, (*it).m_fromId, (*it).m_toSlot, (*it).m_toId, (*it).m_range);

		m_dmr2RFRewrites.push_back(rewrite);
	}

	std::vector<CTypeRewriteStruct> typeRewrites = m_conf.getDMRNetwork2TypeRewrites();
	for (std::vector<CTypeRewriteStruct>::const_iterator it = typeRewrites.begin(); it != typeRewrites.end(); ++it) {
		if ((*it).m_range == 1)
			LogInfo("    Rewrite RF: %u:TG%u -> %u:%u", (*it).m_fromSlot, (*it).m_fromTG, (*it).m_toSlot, (*it).m_toId);
		else
			LogInfo("    Rewrite RF: %u:TG%u-%u -> %u:%u-%u", (*it).m_fromSlot, (*it).m_fromTG, (*it).m_fromTG + (*it).m_range - 1U, (*it).m_toSlot, (*it).m_toId, (*it).m_toId + (*it).m_range - 1U);

		CRewriteType* rewrite = new CRewriteType(m_dmr2Name, (*it).m_fromSlot, (*it).m_fromTG, (*it).m_toSlot, (*it).m_toId, (*it).m_range);

		m_dmr2RFRewrites.push_back(rewrite);
	}

	std::vector<CSrcRewriteStruct> srcRewrites = m_conf.getDMRNetwork2SrcRewrites();
	for (std::vector<CSrcRewriteStruct>::const_iterator it = srcRewrites.begin(); it != srcRewrites.end(); ++it) {
		if ((*it).m_range == 1)
			LogInfo("    Rewrite Net: %u:%u -> %u:TG%u", (*it).m_fromSlot, (*it).m_fromId, (*it).m_toSlot, (*it).m_toTG);
		else
			LogInfo("    Rewrite Net: %u:%u-%u -> %u:TG%u", (*it).m_fromSlot, (*it).m_fromId, (*it).m_fromId + (*it).m_range - 1U, (*it).m_toSlot, (*it).m_toTG);

		CRewriteSrc* rewrite = new CRewriteSrc(m_dmr2Name, (*it).m_fromSlot, (*it).m_fromId, (*it).m_toSlot, (*it).m_toTG, (*it).m_range);

		m_dmr2NetRewrites.push_back(rewrite);
	}

	std::vector<CTGDynRewriteStruct> dynRewrites = m_conf.getDMRNetwork2TGDynRewrites();
	for (std::vector<CTGDynRewriteStruct>::const_iterator it = dynRewrites.begin(); it != dynRewrites.end(); ++it) {
		LogInfo("    Dyn Rewrite: %u:TG%u-%u:TG%u <-> %u:TG%u (disc %u:%u) (status %u:%u) (%u exclusions)", (*it).m_slot, (*it).m_fromTG, (*it).m_slot, (*it).m_fromTG + (*it).m_range - 1U, (*it).m_slot, (*it).m_toTG, (*it).m_slot, (*it).m_discPC, (*it).m_slot, (*it).m_statusPC, (*it).m_exclTGs.size());

		CDynVoice* voice = NULL;
		if (m_conf.getVoiceEnabled()) {
			std::string language = m_conf.getVoiceLanguage();
			std::string directory = m_conf.getVoiceDirectory();

			voice = new CDynVoice(directory, language, m_repeater->getId(), (*it).m_slot, (*it).m_toTG);
			bool ret = voice->open();
			if (!ret) {
				delete voice;
				voice = NULL;
			} else {
				m_dynVoices.push_back(voice);
			}
		}

		CRewriteDynTGNet* netRewriteDynTG = new CRewriteDynTGNet(m_dmr2Name, (*it).m_slot, (*it).m_toTG);
		CRewriteDynTGRF* rfRewriteDynTG = new CRewriteDynTGRF(m_dmr2Name, (*it).m_slot, (*it).m_fromTG, (*it).m_toTG, (*it).m_discPC, (*it).m_statusPC, (*it).m_range, (*it).m_exclTGs, netRewriteDynTG, voice);

		m_dmr2RFRewrites.push_back(rfRewriteDynTG);
		m_dmr2NetRewrites.push_back(netRewriteDynTG);
		m_dynRF.push_back(rfRewriteDynTG);
	}

	std::vector<CIdRewriteStruct> idRewrites = m_conf.getDMRNetwork2IdRewrites();
	for (std::vector<CIdRewriteStruct>::const_iterator it = idRewrites.begin(); it != idRewrites.end(); ++it) {
		LogInfo("    Rewrite Id: %u <-> %u", (*it).m_rfId, (*it).m_netId);

		CRewriteSrcId* rewriteSrcId = new CRewriteSrcId(m_dmr2Name, (*it).m_rfId, (*it).m_netId);
		CRewriteDstId* rewriteDstId = new CRewriteDstId(m_dmr2Name, (*it).m_netId, (*it).m_rfId);

		m_dmr2SrcRewrites.push_back(rewriteSrcId);
		m_dmr2NetRewrites.push_back(rewriteDstId);
	}

	std::vector<unsigned int> tgPassAll = m_conf.getDMRNetwork2PassAllTG();
	for (std::vector<unsigned int>::const_iterator it = tgPassAll.begin(); it != tgPassAll.end(); ++it) {
		LogInfo("    Pass All TG: %u", *it);

		CPassAllTG* rfPassAllTG  = new CPassAllTG(m_dmr2Name, *it);
		CPassAllTG* netPassAllTG = new CPassAllTG(m_dmr2Name, *it);

		m_dmr2Passalls.push_back(rfPassAllTG);
		m_dmr2NetRewrites.push_back(netPassAllTG);
	}

	std::vector<unsigned int> pcPassAll = m_conf.getDMRNetwork2PassAllPC();
	for (std::vector<unsigned int>::const_iterator it = pcPassAll.begin(); it != pcPassAll.end(); ++it) {
		LogInfo("    Pass All PC: %u", *it);

		CPassAllPC* rfPassAllPC  = new CPassAllPC(m_dmr2Name, *it);
		CPassAllPC* netPassAllPC = new CPassAllPC(m_dmr2Name, *it);

		m_dmr2Passalls.push_back(rfPassAllPC);
		m_dmr2NetRewrites.push_back(netPassAllPC);
	}

	return true;
}

bool CDMRGateway::createDMRNetwork3()
{
	std::string address  = m_conf.getDMRNetwork3Address();
	unsigned int port    = m_conf.getDMRNetwork3Port();
	unsigned int local   = m_conf.getDMRNetwork3Local();
	unsigned int id      = m_conf.getDMRNetwork3Id();
	std::string password = m_conf.getDMRNetwork3Password();
	bool location        = m_conf.getDMRNetwork3Location();
	bool debug           = m_conf.getDMRNetwork3Debug();
	m_dmr3Name           = m_conf.getDMRNetwork3Name();

	if (id == 0U)
		id = m_repeater->getId();

	LogInfo("DMR Network 3 Parameters");
	LogInfo("    Name: %s", m_dmr3Name.c_str());
	LogInfo("    Id: %u", id);
	LogInfo("    Address: %s", address.c_str());
	LogInfo("    Port: %u", port);
	if (local > 0U)
		LogInfo("    Local: %u", local);
	else
		LogInfo("    Local: random");
	LogInfo("    Location Data: %s", location ? "yes" : "no");

	m_dmrNetwork3 = new CDMRNetwork(address, port, local, id, password, m_dmr3Name, VERSION, debug);

	std::string options = m_conf.getDMRNetwork3Options();
	if (options.empty())
		options = m_repeater->getOptions();

	if (!options.empty()) {
		LogInfo("    Options: %s", options.c_str());
		m_dmrNetwork3->setOptions(options);
	}

	unsigned char config[400U];
	unsigned int len = getConfig(m_dmr3Name, config);

	if (!location)
		::memcpy(config + 30U, "0.00000000.000000", 17U);

	m_dmrNetwork3->setConfig(config, len);

	bool ret = m_dmrNetwork3->open();
	if (!ret) {
		delete m_dmrNetwork3;
		m_dmrNetwork3 = NULL;
		return false;
	}

	std::vector<CTGRewriteStruct> tgRewrites = m_conf.getDMRNetwork3TGRewrites();
	for (std::vector<CTGRewriteStruct>::const_iterator it = tgRewrites.begin(); it != tgRewrites.end(); ++it) {
		if ((*it).m_range == 1)
			LogInfo("    Rewrite RF: %u:TG%u -> %u:TG%u", (*it).m_fromSlot, (*it).m_fromTG, (*it).m_toSlot, (*it).m_toTG);
		else
			LogInfo("    Rewrite RF: %u:TG%u-TG%u -> %u:TG%u-TG%u", (*it).m_fromSlot, (*it).m_fromTG, (*it).m_fromTG + (*it).m_range - 1U, (*it).m_toSlot, (*it).m_toTG, (*it).m_toTG + (*it).m_range - 1U);
		if ((*it).m_range == 1)
			LogInfo("    Rewrite Net: %u:TG%u -> %u:TG%u", (*it).m_toSlot, (*it).m_toTG, (*it).m_fromSlot, (*it).m_fromTG);
		else
			LogInfo("    Rewrite Net: %u:TG%u-TG%u -> %u:TG%u-TG%u", (*it).m_toSlot, (*it).m_toTG, (*it).m_toTG + (*it).m_range - 1U, (*it).m_fromSlot, (*it).m_fromTG, (*it).m_fromTG + (*it).m_range - 1U);

		CRewriteTG* rfRewrite = new CRewriteTG(m_dmr3Name, (*it).m_fromSlot, (*it).m_fromTG, (*it).m_toSlot, (*it).m_toTG, (*it).m_range);
		CRewriteTG* netRewrite = new CRewriteTG(m_dmr3Name, (*it).m_toSlot, (*it).m_toTG, (*it).m_fromSlot, (*it).m_fromTG, (*it).m_range);

		m_dmr3RFRewrites.push_back(rfRewrite);
		m_dmr3NetRewrites.push_back(netRewrite);
	}

	std::vector<CPCRewriteStruct> pcRewrites = m_conf.getDMRNetwork3PCRewrites();
	for (std::vector<CPCRewriteStruct>::const_iterator it = pcRewrites.begin(); it != pcRewrites.end(); ++it) {
		if ((*it).m_range == 1)
			LogInfo("    Rewrite RF: %u:%u -> %u:%u", (*it).m_fromSlot, (*it).m_fromId, (*it).m_toSlot, (*it).m_toId);
		else
			LogInfo("    Rewrite RF: %u:%u-%u -> %u:%u-%u", (*it).m_fromSlot, (*it).m_fromId, (*it).m_fromId + (*it).m_range - 1U, (*it).m_toSlot, (*it).m_toId, (*it).m_toId + (*it).m_range - 1U);

		CRewritePC* rewrite = new CRewritePC(m_dmr3Name, (*it).m_fromSlot, (*it).m_fromId, (*it).m_toSlot, (*it).m_toId, (*it).m_range);

		m_dmr3RFRewrites.push_back(rewrite);
	}

	std::vector<CTypeRewriteStruct> typeRewrites = m_conf.getDMRNetwork3TypeRewrites();
	for (std::vector<CTypeRewriteStruct>::const_iterator it = typeRewrites.begin(); it != typeRewrites.end(); ++it) {
		if ((*it).m_range == 1)
			LogInfo("    Rewrite RF: %u:TG%u -> %u:%u", (*it).m_fromSlot, (*it).m_fromTG, (*it).m_toSlot, (*it).m_toId);
		else
			LogInfo("    Rewrite RF: %u:TG%u-%u -> %u:%u-%u", (*it).m_fromSlot, (*it).m_fromTG, (*it).m_fromTG + (*it).m_range - 1U, (*it).m_toSlot, (*it).m_toId, (*it).m_toId + (*it).m_range - 1U);

		CRewriteType* rewrite = new CRewriteType(m_dmr3Name, (*it).m_fromSlot, (*it).m_fromTG, (*it).m_toSlot, (*it).m_toId, (*it).m_range);

		m_dmr3RFRewrites.push_back(rewrite);
	}

	std::vector<CSrcRewriteStruct> srcRewrites = m_conf.getDMRNetwork3SrcRewrites();
	for (std::vector<CSrcRewriteStruct>::const_iterator it = srcRewrites.begin(); it != srcRewrites.end(); ++it) {
		if ((*it).m_range == 1)
			LogInfo("    Rewrite Net: %u:%u -> %u:TG%u", (*it).m_fromSlot, (*it).m_fromId, (*it).m_toSlot, (*it).m_toTG);
		else
			LogInfo("    Rewrite Net: %u:%u-%u -> %u:TG%u", (*it).m_fromSlot, (*it).m_fromId, (*it).m_fromId + (*it).m_range - 1U, (*it).m_toSlot, (*it).m_toTG);

		CRewriteSrc* rewrite = new CRewriteSrc(m_dmr3Name, (*it).m_fromSlot, (*it).m_fromId, (*it).m_toSlot, (*it).m_toTG, (*it).m_range);

		m_dmr3NetRewrites.push_back(rewrite);
	}

	std::vector<CTGDynRewriteStruct> dynRewrites = m_conf.getDMRNetwork3TGDynRewrites();
	for (std::vector<CTGDynRewriteStruct>::const_iterator it = dynRewrites.begin(); it != dynRewrites.end(); ++it) {
		LogInfo("    Dyn Rewrite: %u:TG%u-%u:TG%u <-> %u:TG%u (disc %u:%u) (status %u:%u) (%u exclusions)", (*it).m_slot, (*it).m_fromTG, (*it).m_slot, (*it).m_fromTG + (*it).m_range - 1U, (*it).m_slot, (*it).m_toTG, (*it).m_slot, (*it).m_discPC, (*it).m_slot, (*it).m_statusPC, (*it).m_exclTGs.size());

		CDynVoice* voice = NULL;
		if (m_conf.getVoiceEnabled()) {
			std::string language = m_conf.getVoiceLanguage();
			std::string directory = m_conf.getVoiceDirectory();

			voice = new CDynVoice(directory, language, m_repeater->getId(), (*it).m_slot, (*it).m_toTG);
			bool ret = voice->open();
			if (!ret) {
				delete voice;
				voice = NULL;
			} else {
				m_dynVoices.push_back(voice);
			}
		}

		CRewriteDynTGNet* netRewriteDynTG = new CRewriteDynTGNet(m_dmr3Name, (*it).m_slot, (*it).m_toTG);
		CRewriteDynTGRF* rfRewriteDynTG = new CRewriteDynTGRF(m_dmr3Name, (*it).m_slot, (*it).m_fromTG, (*it).m_toTG, (*it).m_discPC, (*it).m_statusPC, (*it).m_range, (*it).m_exclTGs, netRewriteDynTG, voice);

		m_dmr3RFRewrites.push_back(rfRewriteDynTG);
		m_dmr3NetRewrites.push_back(netRewriteDynTG);
		m_dynRF.push_back(rfRewriteDynTG);
	}

	std::vector<CIdRewriteStruct> idRewrites = m_conf.getDMRNetwork3IdRewrites();
	for (std::vector<CIdRewriteStruct>::const_iterator it = idRewrites.begin(); it != idRewrites.end(); ++it) {
		LogInfo("    Rewrite Id: %u <-> %u", (*it).m_rfId, (*it).m_netId);

		CRewriteSrcId* rewriteSrcId = new CRewriteSrcId(m_dmr3Name, (*it).m_rfId, (*it).m_netId);
		CRewriteDstId* rewriteDstId = new CRewriteDstId(m_dmr3Name, (*it).m_netId, (*it).m_rfId);

		m_dmr3SrcRewrites.push_back(rewriteSrcId);
		m_dmr3NetRewrites.push_back(rewriteDstId);
	}

	std::vector<unsigned int> tgPassAll = m_conf.getDMRNetwork3PassAllTG();
	for (std::vector<unsigned int>::const_iterator it = tgPassAll.begin(); it != tgPassAll.end(); ++it) {
		LogInfo("    Pass All TG: %u", *it);

		CPassAllTG* rfPassAllTG = new CPassAllTG(m_dmr3Name, *it);
		CPassAllTG* netPassAllTG = new CPassAllTG(m_dmr3Name, *it);

		m_dmr3Passalls.push_back(rfPassAllTG);
		m_dmr3NetRewrites.push_back(netPassAllTG);
	}

	std::vector<unsigned int> pcPassAll = m_conf.getDMRNetwork3PassAllPC();
	for (std::vector<unsigned int>::const_iterator it = pcPassAll.begin(); it != pcPassAll.end(); ++it) {
		LogInfo("    Pass All PC: %u", *it);

		CPassAllPC* rfPassAllPC = new CPassAllPC(m_dmr3Name, *it);
		CPassAllPC* netPassAllPC = new CPassAllPC(m_dmr3Name, *it);

		m_dmr3Passalls.push_back(rfPassAllPC);
		m_dmr3NetRewrites.push_back(netPassAllPC);
	}

	return true;
}

bool CDMRGateway::createDMRNetwork4()
{
	std::string address  = m_conf.getDMRNetwork4Address();
	unsigned int port    = m_conf.getDMRNetwork4Port();
	unsigned int local   = m_conf.getDMRNetwork4Local();
	unsigned int id      = m_conf.getDMRNetwork4Id();
	std::string password = m_conf.getDMRNetwork4Password();
	bool location        = m_conf.getDMRNetwork4Location();
	bool debug           = m_conf.getDMRNetwork4Debug();
	m_dmr4Name           = m_conf.getDMRNetwork4Name();

	if (id == 0U)
		id = m_repeater->getId();

	LogInfo("DMR Network 4 Parameters");
	LogInfo("    Name: %s", m_dmr4Name.c_str());
	LogInfo("    Id: %u", id);
	LogInfo("    Address: %s", address.c_str());
	LogInfo("    Port: %u", port);
	if (local > 0U)
		LogInfo("    Local: %u", local);
	else
		LogInfo("    Local: random");
	LogInfo("    Location Data: %s", location ? "yes" : "no");

	m_dmrNetwork4 = new CDMRNetwork(address, port, local, id, password, m_dmr4Name, VERSION, debug);

	std::string options = m_conf.getDMRNetwork4Options();
	if (options.empty())
		options = m_repeater->getOptions();

	if (!options.empty()) {
		LogInfo("    Options: %s", options.c_str());
		m_dmrNetwork4->setOptions(options);
	}

	unsigned char config[400U];
	unsigned int len = getConfig(m_dmr4Name, config);

	if (!location)
		::memcpy(config + 30U, "0.00000000.000000", 17U);

	m_dmrNetwork4->setConfig(config, len);

	bool ret = m_dmrNetwork4->open();
	if (!ret) {
		delete m_dmrNetwork4;
		m_dmrNetwork4 = NULL;
		return false;
	}

	std::vector<CTGRewriteStruct> tgRewrites = m_conf.getDMRNetwork4TGRewrites();
	for (std::vector<CTGRewriteStruct>::const_iterator it = tgRewrites.begin(); it != tgRewrites.end(); ++it) {
		if ((*it).m_range == 1)
			LogInfo("    Rewrite RF: %u:TG%u -> %u:TG%u", (*it).m_fromSlot, (*it).m_fromTG, (*it).m_toSlot, (*it).m_toTG);
		else
			LogInfo("    Rewrite RF: %u:TG%u-TG%u -> %u:TG%u-TG%u", (*it).m_fromSlot, (*it).m_fromTG, (*it).m_fromTG + (*it).m_range - 1U, (*it).m_toSlot, (*it).m_toTG, (*it).m_toTG + (*it).m_range - 1U);
		if ((*it).m_range == 1)
			LogInfo("    Rewrite Net: %u:TG%u -> %u:TG%u", (*it).m_toSlot, (*it).m_toTG, (*it).m_fromSlot, (*it).m_fromTG);
		else
			LogInfo("    Rewrite Net: %u:TG%u-TG%u -> %u:TG%u-TG%u", (*it).m_toSlot, (*it).m_toTG, (*it).m_toTG + (*it).m_range - 1U, (*it).m_fromSlot, (*it).m_fromTG, (*it).m_fromTG + (*it).m_range - 1U);

		CRewriteTG* rfRewrite = new CRewriteTG(m_dmr4Name, (*it).m_fromSlot, (*it).m_fromTG, (*it).m_toSlot, (*it).m_toTG, (*it).m_range);
		CRewriteTG* netRewrite = new CRewriteTG(m_dmr4Name, (*it).m_toSlot, (*it).m_toTG, (*it).m_fromSlot, (*it).m_fromTG, (*it).m_range);

		m_dmr4RFRewrites.push_back(rfRewrite);
		m_dmr4NetRewrites.push_back(netRewrite);
	}

	std::vector<CPCRewriteStruct> pcRewrites = m_conf.getDMRNetwork4PCRewrites();
	for (std::vector<CPCRewriteStruct>::const_iterator it = pcRewrites.begin(); it != pcRewrites.end(); ++it) {
		if ((*it).m_range == 1)
			LogInfo("    Rewrite RF: %u:%u -> %u:%u", (*it).m_fromSlot, (*it).m_fromId, (*it).m_toSlot, (*it).m_toId);
		else
			LogInfo("    Rewrite RF: %u:%u-%u -> %u:%u-%u", (*it).m_fromSlot, (*it).m_fromId, (*it).m_fromId + (*it).m_range - 1U, (*it).m_toSlot, (*it).m_toId, (*it).m_toId + (*it).m_range - 1U);

		CRewritePC* rewrite = new CRewritePC(m_dmr4Name, (*it).m_fromSlot, (*it).m_fromId, (*it).m_toSlot, (*it).m_toId, (*it).m_range);

		m_dmr4RFRewrites.push_back(rewrite);
	}

	std::vector<CTypeRewriteStruct> typeRewrites = m_conf.getDMRNetwork4TypeRewrites();
	for (std::vector<CTypeRewriteStruct>::const_iterator it = typeRewrites.begin(); it != typeRewrites.end(); ++it) {
		if ((*it).m_range == 1)
			LogInfo("    Rewrite RF: %u:TG%u -> %u:%u", (*it).m_fromSlot, (*it).m_fromTG, (*it).m_toSlot, (*it).m_toId);
		else
			LogInfo("    Rewrite RF: %u:TG%u-%u -> %u:%u-%u", (*it).m_fromSlot, (*it).m_fromTG, (*it).m_fromTG + (*it).m_range - 1U, (*it).m_toSlot, (*it).m_toId, (*it).m_toId + (*it).m_range - 1U);

		CRewriteType* rewrite = new CRewriteType(m_dmr4Name, (*it).m_fromSlot, (*it).m_fromTG, (*it).m_toSlot, (*it).m_toId, (*it).m_range);

		m_dmr4RFRewrites.push_back(rewrite);
	}

	std::vector<CSrcRewriteStruct> srcRewrites = m_conf.getDMRNetwork4SrcRewrites();
	for (std::vector<CSrcRewriteStruct>::const_iterator it = srcRewrites.begin(); it != srcRewrites.end(); ++it) {
		if ((*it).m_range == 1)
			LogInfo("    Rewrite Net: %u:%u -> %u:TG%u", (*it).m_fromSlot, (*it).m_fromId, (*it).m_toSlot, (*it).m_toTG);
		else
			LogInfo("    Rewrite Net: %u:%u-%u -> %u:TG%u", (*it).m_fromSlot, (*it).m_fromId, (*it).m_fromId + (*it).m_range - 1U, (*it).m_toSlot, (*it).m_toTG);

		CRewriteSrc* rewrite = new CRewriteSrc(m_dmr4Name, (*it).m_fromSlot, (*it).m_fromId, (*it).m_toSlot, (*it).m_toTG, (*it).m_range);

		m_dmr4NetRewrites.push_back(rewrite);
	}

	std::vector<CTGDynRewriteStruct> dynRewrites = m_conf.getDMRNetwork4TGDynRewrites();
	for (std::vector<CTGDynRewriteStruct>::const_iterator it = dynRewrites.begin(); it != dynRewrites.end(); ++it) {
		LogInfo("    Dyn Rewrite: %u:TG%u-%u:TG%u <-> %u:TG%u (disc %u:%u) (status %u:%u) (%u exclusions)", (*it).m_slot, (*it).m_fromTG, (*it).m_slot, (*it).m_fromTG + (*it).m_range - 1U, (*it).m_slot, (*it).m_toTG, (*it).m_slot, (*it).m_discPC, (*it).m_slot, (*it).m_statusPC, (*it).m_exclTGs.size());

		CDynVoice* voice = NULL;
		if (m_conf.getVoiceEnabled()) {
			std::string language = m_conf.getVoiceLanguage();
			std::string directory = m_conf.getVoiceDirectory();

			voice = new CDynVoice(directory, language, m_repeater->getId(), (*it).m_slot, (*it).m_toTG);
			bool ret = voice->open();
			if (!ret) {
				delete voice;
				voice = NULL;
			} else {
				m_dynVoices.push_back(voice);
			}
		}

		CRewriteDynTGNet* netRewriteDynTG = new CRewriteDynTGNet(m_dmr4Name, (*it).m_slot, (*it).m_toTG);
		CRewriteDynTGRF* rfRewriteDynTG = new CRewriteDynTGRF(m_dmr4Name, (*it).m_slot, (*it).m_fromTG, (*it).m_toTG, (*it).m_discPC, (*it).m_statusPC, (*it).m_range, (*it).m_exclTGs, netRewriteDynTG, voice);

		m_dmr4RFRewrites.push_back(rfRewriteDynTG);
		m_dmr4NetRewrites.push_back(netRewriteDynTG);
		m_dynRF.push_back(rfRewriteDynTG);
	}

	std::vector<CIdRewriteStruct> idRewrites = m_conf.getDMRNetwork4IdRewrites();
	for (std::vector<CIdRewriteStruct>::const_iterator it = idRewrites.begin(); it != idRewrites.end(); ++it) {
		LogInfo("    Rewrite Id: %u <-> %u", (*it).m_rfId, (*it).m_netId);

		CRewriteSrcId* rewriteSrcId = new CRewriteSrcId(m_dmr4Name, (*it).m_rfId, (*it).m_netId);
		CRewriteDstId* rewriteDstId = new CRewriteDstId(m_dmr4Name, (*it).m_netId, (*it).m_rfId);

		m_dmr4SrcRewrites.push_back(rewriteSrcId);
		m_dmr4NetRewrites.push_back(rewriteDstId);
	}

	std::vector<unsigned int> tgPassAll = m_conf.getDMRNetwork4PassAllTG();
	for (std::vector<unsigned int>::const_iterator it = tgPassAll.begin(); it != tgPassAll.end(); ++it) {
		LogInfo("    Pass All TG: %u", *it);

		CPassAllTG* rfPassAllTG = new CPassAllTG(m_dmr4Name, *it);
		CPassAllTG* netPassAllTG = new CPassAllTG(m_dmr4Name, *it);

		m_dmr4Passalls.push_back(rfPassAllTG);
		m_dmr4NetRewrites.push_back(netPassAllTG);
	}

	std::vector<unsigned int> pcPassAll = m_conf.getDMRNetwork4PassAllPC();
	for (std::vector<unsigned int>::const_iterator it = pcPassAll.begin(); it != pcPassAll.end(); ++it) {
		LogInfo("    Pass All PC: %u", *it);

		CPassAllPC* rfPassAllPC = new CPassAllPC(m_dmr4Name, *it);
		CPassAllPC* netPassAllPC = new CPassAllPC(m_dmr4Name, *it);

		m_dmr4Passalls.push_back(rfPassAllPC);
		m_dmr4NetRewrites.push_back(netPassAllPC);
	}

	return true;
}


//////////////////////////
bool CDMRGateway::createDMRNetwork5()
{
	std::string address  = m_conf.getDMRNetwork5Address();
	unsigned int port    = m_conf.getDMRNetwork5Port();
	unsigned int local   = m_conf.getDMRNetwork5Local();
	unsigned int id      = m_conf.getDMRNetwork5Id();
	std::string password = m_conf.getDMRNetwork5Password();
	bool location        = m_conf.getDMRNetwork5Location();
	bool debug           = m_conf.getDMRNetwork5Debug();
	m_dmr5Name           = m_conf.getDMRNetwork5Name();

	if (id == 0U)
		id = m_repeater->getId();

	LogInfo("DMR Network 5 Parameters");
	LogInfo("    Name: %s", m_dmr5Name.c_str());
	LogInfo("    Id: %u", id);
	LogInfo("    Address: %s", address.c_str());
	LogInfo("    Port: %u", port);
	if (local > 0U)
		LogInfo("    Local: %u", local);
	else
		LogInfo("    Local: random");
	LogInfo("    Location Data: %s", location ? "yes" : "no");

	m_dmrNetwork5 = new CDMRNetwork(address, port, local, id, password, m_dmr5Name, VERSION, debug);

	std::string options = m_conf.getDMRNetwork5Options();
	if (options.empty())
		options = m_repeater->getOptions();

	if (!options.empty()) {
		LogInfo("    Options: %s", options.c_str());
		m_dmrNetwork5->setOptions(options);
	}

	unsigned char config[400U];
	unsigned int len = getConfig(m_dmr5Name, config);

	if (!location)
		::memcpy(config + 30U, "0.00000000.000000", 17U);

	m_dmrNetwork5->setConfig(config, len);

	bool ret = m_dmrNetwork5->open();
	if (!ret) {
		delete m_dmrNetwork5;
		m_dmrNetwork5 = NULL;
		return false;
	}

	std::vector<CTGRewriteStruct> tgRewrites = m_conf.getDMRNetwork5TGRewrites();
	for (std::vector<CTGRewriteStruct>::const_iterator it = tgRewrites.begin(); it != tgRewrites.end(); ++it) {
		if ((*it).m_range == 1)
			LogInfo("    Rewrite RF: %u:TG%u -> %u:TG%u", (*it).m_fromSlot, (*it).m_fromTG, (*it).m_toSlot, (*it).m_toTG);
		else
			LogInfo("    Rewrite RF: %u:TG%u-TG%u -> %u:TG%u-TG%u", (*it).m_fromSlot, (*it).m_fromTG, (*it).m_fromTG + (*it).m_range - 1U, (*it).m_toSlot, (*it).m_toTG, (*it).m_toTG + (*it).m_range - 1U);
		if ((*it).m_range == 1)
			LogInfo("    Rewrite Net: %u:TG%u -> %u:TG%u", (*it).m_toSlot, (*it).m_toTG, (*it).m_fromSlot, (*it).m_fromTG);
		else
			LogInfo("    Rewrite Net: %u:TG%u-TG%u -> %u:TG%u-TG%u", (*it).m_toSlot, (*it).m_toTG, (*it).m_toTG + (*it).m_range - 1U, (*it).m_fromSlot, (*it).m_fromTG, (*it).m_fromTG + (*it).m_range - 1U);

		CRewriteTG* rfRewrite = new CRewriteTG(m_dmr5Name, (*it).m_fromSlot, (*it).m_fromTG, (*it).m_toSlot, (*it).m_toTG, (*it).m_range);
		CRewriteTG* netRewrite = new CRewriteTG(m_dmr5Name, (*it).m_toSlot, (*it).m_toTG, (*it).m_fromSlot, (*it).m_fromTG, (*it).m_range);

		m_dmr5RFRewrites.push_back(rfRewrite);
		m_dmr5NetRewrites.push_back(netRewrite);
	}

	std::vector<CPCRewriteStruct> pcRewrites = m_conf.getDMRNetwork5PCRewrites();
	for (std::vector<CPCRewriteStruct>::const_iterator it = pcRewrites.begin(); it != pcRewrites.end(); ++it) {
		if ((*it).m_range == 1)
			LogInfo("    Rewrite RF: %u:%u -> %u:%u", (*it).m_fromSlot, (*it).m_fromId, (*it).m_toSlot, (*it).m_toId);
		else
			LogInfo("    Rewrite RF: %u:%u-%u -> %u:%u-%u", (*it).m_fromSlot, (*it).m_fromId, (*it).m_fromId + (*it).m_range - 1U, (*it).m_toSlot, (*it).m_toId, (*it).m_toId + (*it).m_range - 1U);

		CRewritePC* rewrite = new CRewritePC(m_dmr5Name, (*it).m_fromSlot, (*it).m_fromId, (*it).m_toSlot, (*it).m_toId, (*it).m_range);

		m_dmr5RFRewrites.push_back(rewrite);
	}

	std::vector<CTypeRewriteStruct> typeRewrites = m_conf.getDMRNetwork5TypeRewrites();
	for (std::vector<CTypeRewriteStruct>::const_iterator it = typeRewrites.begin(); it != typeRewrites.end(); ++it) {
		if ((*it).m_range == 1)
			LogInfo("    Rewrite RF: %u:TG%u -> %u:%u", (*it).m_fromSlot, (*it).m_fromTG, (*it).m_toSlot, (*it).m_toId);
		else
			LogInfo("    Rewrite RF: %u:TG%u-%u -> %u:%u-%u", (*it).m_fromSlot, (*it).m_fromTG, (*it).m_fromTG + (*it).m_range - 1U, (*it).m_toSlot, (*it).m_toId, (*it).m_toId + (*it).m_range - 1U);

		CRewriteType* rewrite = new CRewriteType(m_dmr5Name, (*it).m_fromSlot, (*it).m_fromTG, (*it).m_toSlot, (*it).m_toId, (*it).m_range);

		m_dmr5RFRewrites.push_back(rewrite);
	}

	std::vector<CSrcRewriteStruct> srcRewrites = m_conf.getDMRNetwork5SrcRewrites();
	for (std::vector<CSrcRewriteStruct>::const_iterator it = srcRewrites.begin(); it != srcRewrites.end(); ++it) {
		if ((*it).m_range == 1)
			LogInfo("    Rewrite Net: %u:%u -> %u:TG%u", (*it).m_fromSlot, (*it).m_fromId, (*it).m_toSlot, (*it).m_toTG);
		else
			LogInfo("    Rewrite Net: %u:%u-%u -> %u:TG%u", (*it).m_fromSlot, (*it).m_fromId, (*it).m_fromId + (*it).m_range - 1U, (*it).m_toSlot, (*it).m_toTG);

		CRewriteSrc* rewrite = new CRewriteSrc(m_dmr5Name, (*it).m_fromSlot, (*it).m_fromId, (*it).m_toSlot, (*it).m_toTG, (*it).m_range);

		m_dmr5NetRewrites.push_back(rewrite);
	}

	std::vector<CTGDynRewriteStruct> dynRewrites = m_conf.getDMRNetwork5TGDynRewrites();
	for (std::vector<CTGDynRewriteStruct>::const_iterator it = dynRewrites.begin(); it != dynRewrites.end(); ++it) {
		LogInfo("    Dyn Rewrite: %u:TG%u-%u:TG%u <-> %u:TG%u (disc %u:%u) (status %u:%u) (%u exclusions)", (*it).m_slot, (*it).m_fromTG, (*it).m_slot, (*it).m_fromTG + (*it).m_range - 1U, (*it).m_slot, (*it).m_toTG, (*it).m_slot, (*it).m_discPC, (*it).m_slot, (*it).m_statusPC, (*it).m_exclTGs.size());

		CDynVoice* voice = NULL;
		if (m_conf.getVoiceEnabled()) {
			std::string language = m_conf.getVoiceLanguage();
			std::string directory = m_conf.getVoiceDirectory();

			voice = new CDynVoice(directory, language, m_repeater->getId(), (*it).m_slot, (*it).m_toTG);
			bool ret = voice->open();
			if (!ret) {
				delete voice;
				voice = NULL;
			} else {
				m_dynVoices.push_back(voice);
			}
		}

		CRewriteDynTGNet* netRewriteDynTG = new CRewriteDynTGNet(m_dmr5Name, (*it).m_slot, (*it).m_toTG);
		CRewriteDynTGRF* rfRewriteDynTG = new CRewriteDynTGRF(m_dmr5Name, (*it).m_slot, (*it).m_fromTG, (*it).m_toTG, (*it).m_discPC, (*it).m_statusPC, (*it).m_range, (*it).m_exclTGs, netRewriteDynTG, voice);

		m_dmr5RFRewrites.push_back(rfRewriteDynTG);
		m_dmr5NetRewrites.push_back(netRewriteDynTG);
		m_dynRF.push_back(rfRewriteDynTG);
	}

	std::vector<CIdRewriteStruct> idRewrites = m_conf.getDMRNetwork5IdRewrites();
	for (std::vector<CIdRewriteStruct>::const_iterator it = idRewrites.begin(); it != idRewrites.end(); ++it) {
		LogInfo("    Rewrite Id: %u <-> %u", (*it).m_rfId, (*it).m_netId);

		CRewriteSrcId* rewriteSrcId = new CRewriteSrcId(m_dmr5Name, (*it).m_rfId, (*it).m_netId);
		CRewriteDstId* rewriteDstId = new CRewriteDstId(m_dmr5Name, (*it).m_netId, (*it).m_rfId);

		m_dmr5SrcRewrites.push_back(rewriteSrcId);
		m_dmr5NetRewrites.push_back(rewriteDstId);
	}

	std::vector<unsigned int> tgPassAll = m_conf.getDMRNetwork5PassAllTG();
	for (std::vector<unsigned int>::const_iterator it = tgPassAll.begin(); it != tgPassAll.end(); ++it) {
		LogInfo("    Pass All TG: %u", *it);

		CPassAllTG* rfPassAllTG = new CPassAllTG(m_dmr5Name, *it);
		CPassAllTG* netPassAllTG = new CPassAllTG(m_dmr5Name, *it);

		m_dmr5Passalls.push_back(rfPassAllTG);
		m_dmr5NetRewrites.push_back(netPassAllTG);
	}

	std::vector<unsigned int> pcPassAll = m_conf.getDMRNetwork5PassAllPC();
	for (std::vector<unsigned int>::const_iterator it = pcPassAll.begin(); it != pcPassAll.end(); ++it) {
		LogInfo("    Pass All PC: %u", *it);

		CPassAllPC* rfPassAllPC = new CPassAllPC(m_dmr5Name, *it);
		CPassAllPC* netPassAllPC = new CPassAllPC(m_dmr5Name, *it);

		m_dmr5Passalls.push_back(rfPassAllPC);
		m_dmr5NetRewrites.push_back(netPassAllPC);
	}

	return true;
}
bool CDMRGateway::createDMRNetwork6()
{
	std::string address  = m_conf.getDMRNetwork6Address();
	unsigned int port    = m_conf.getDMRNetwork6Port();
	unsigned int local   = m_conf.getDMRNetwork6Local();
	unsigned int id      = m_conf.getDMRNetwork6Id();
	std::string password = m_conf.getDMRNetwork6Password();
	bool location        = m_conf.getDMRNetwork6Location();
	bool debug           = m_conf.getDMRNetwork6Debug();
	m_dmr6Name           = m_conf.getDMRNetwork6Name();

	if (id == 0U)
		id = m_repeater->getId();

	LogInfo("DMR Network 6 Parameters");
	LogInfo("    Name: %s", m_dmr6Name.c_str());
	LogInfo("    Id: %u", id);
	LogInfo("    Address: %s", address.c_str());
	LogInfo("    Port: %u", port);
	if (local > 0U)
		LogInfo("    Local: %u", local);
	else
		LogInfo("    Local: random");
	LogInfo("    Location Data: %s", location ? "yes" : "no");

	m_dmrNetwork6 = new CDMRNetwork(address, port, local, id, password, m_dmr6Name, VERSION, debug);

	std::string options = m_conf.getDMRNetwork6Options();
	if (options.empty())
		options = m_repeater->getOptions();

	if (!options.empty()) {
		LogInfo("    Options: %s", options.c_str());
		m_dmrNetwork6->setOptions(options);
	}

	unsigned char config[400U];
	unsigned int len = getConfig(m_dmr6Name, config);

	if (!location)
		::memcpy(config + 30U, "0.00000000.000000", 17U);

	m_dmrNetwork6->setConfig(config, len);

	bool ret = m_dmrNetwork6->open();
	if (!ret) {
		delete m_dmrNetwork6;
		m_dmrNetwork6 = NULL;
		return false;
	}

	std::vector<CTGRewriteStruct> tgRewrites = m_conf.getDMRNetwork6TGRewrites();
	for (std::vector<CTGRewriteStruct>::const_iterator it = tgRewrites.begin(); it != tgRewrites.end(); ++it) {
		if ((*it).m_range == 1)
			LogInfo("    Rewrite RF: %u:TG%u -> %u:TG%u", (*it).m_fromSlot, (*it).m_fromTG, (*it).m_toSlot, (*it).m_toTG);
		else
			LogInfo("    Rewrite RF: %u:TG%u-TG%u -> %u:TG%u-TG%u", (*it).m_fromSlot, (*it).m_fromTG, (*it).m_fromTG + (*it).m_range - 1U, (*it).m_toSlot, (*it).m_toTG, (*it).m_toTG + (*it).m_range - 1U);
		if ((*it).m_range == 1)
			LogInfo("    Rewrite Net: %u:TG%u -> %u:TG%u", (*it).m_toSlot, (*it).m_toTG, (*it).m_fromSlot, (*it).m_fromTG);
		else
			LogInfo("    Rewrite Net: %u:TG%u-TG%u -> %u:TG%u-TG%u", (*it).m_toSlot, (*it).m_toTG, (*it).m_toTG + (*it).m_range - 1U, (*it).m_fromSlot, (*it).m_fromTG, (*it).m_fromTG + (*it).m_range - 1U);

		CRewriteTG* rfRewrite = new CRewriteTG(m_dmr6Name, (*it).m_fromSlot, (*it).m_fromTG, (*it).m_toSlot, (*it).m_toTG, (*it).m_range);
		CRewriteTG* netRewrite = new CRewriteTG(m_dmr6Name, (*it).m_toSlot, (*it).m_toTG, (*it).m_fromSlot, (*it).m_fromTG, (*it).m_range);

		m_dmr6RFRewrites.push_back(rfRewrite);
		m_dmr6NetRewrites.push_back(netRewrite);
	}

	std::vector<CPCRewriteStruct> pcRewrites = m_conf.getDMRNetwork6PCRewrites();
	for (std::vector<CPCRewriteStruct>::const_iterator it = pcRewrites.begin(); it != pcRewrites.end(); ++it) {
		if ((*it).m_range == 1)
			LogInfo("    Rewrite RF: %u:%u -> %u:%u", (*it).m_fromSlot, (*it).m_fromId, (*it).m_toSlot, (*it).m_toId);
		else
			LogInfo("    Rewrite RF: %u:%u-%u -> %u:%u-%u", (*it).m_fromSlot, (*it).m_fromId, (*it).m_fromId + (*it).m_range - 1U, (*it).m_toSlot, (*it).m_toId, (*it).m_toId + (*it).m_range - 1U);

		CRewritePC* rewrite = new CRewritePC(m_dmr6Name, (*it).m_fromSlot, (*it).m_fromId, (*it).m_toSlot, (*it).m_toId, (*it).m_range);

		m_dmr6RFRewrites.push_back(rewrite);
	}

	std::vector<CTypeRewriteStruct> typeRewrites = m_conf.getDMRNetwork6TypeRewrites();
	for (std::vector<CTypeRewriteStruct>::const_iterator it = typeRewrites.begin(); it != typeRewrites.end(); ++it) {
		if ((*it).m_range == 1)
			LogInfo("    Rewrite RF: %u:TG%u -> %u:%u", (*it).m_fromSlot, (*it).m_fromTG, (*it).m_toSlot, (*it).m_toId);
		else
			LogInfo("    Rewrite RF: %u:TG%u-%u -> %u:%u-%u", (*it).m_fromSlot, (*it).m_fromTG, (*it).m_fromTG + (*it).m_range - 1U, (*it).m_toSlot, (*it).m_toId, (*it).m_toId + (*it).m_range - 1U);

		CRewriteType* rewrite = new CRewriteType(m_dmr6Name, (*it).m_fromSlot, (*it).m_fromTG, (*it).m_toSlot, (*it).m_toId, (*it).m_range);

		m_dmr6RFRewrites.push_back(rewrite);
	}

	std::vector<CSrcRewriteStruct> srcRewrites = m_conf.getDMRNetwork6SrcRewrites();
	for (std::vector<CSrcRewriteStruct>::const_iterator it = srcRewrites.begin(); it != srcRewrites.end(); ++it) {
		if ((*it).m_range == 1)
			LogInfo("    Rewrite Net: %u:%u -> %u:TG%u", (*it).m_fromSlot, (*it).m_fromId, (*it).m_toSlot, (*it).m_toTG);
		else
			LogInfo("    Rewrite Net: %u:%u-%u -> %u:TG%u", (*it).m_fromSlot, (*it).m_fromId, (*it).m_fromId + (*it).m_range - 1U, (*it).m_toSlot, (*it).m_toTG);

		CRewriteSrc* rewrite = new CRewriteSrc(m_dmr6Name, (*it).m_fromSlot, (*it).m_fromId, (*it).m_toSlot, (*it).m_toTG, (*it).m_range);

		m_dmr6NetRewrites.push_back(rewrite);
	}

	std::vector<CTGDynRewriteStruct> dynRewrites = m_conf.getDMRNetwork6TGDynRewrites();
	for (std::vector<CTGDynRewriteStruct>::const_iterator it = dynRewrites.begin(); it != dynRewrites.end(); ++it) {
		LogInfo("    Dyn Rewrite: %u:TG%u-%u:TG%u <-> %u:TG%u (disc %u:%u) (status %u:%u) (%u exclusions)", (*it).m_slot, (*it).m_fromTG, (*it).m_slot, (*it).m_fromTG + (*it).m_range - 1U, (*it).m_slot, (*it).m_toTG, (*it).m_slot, (*it).m_discPC, (*it).m_slot, (*it).m_statusPC, (*it).m_exclTGs.size());

		CDynVoice* voice = NULL;
		if (m_conf.getVoiceEnabled()) {
			std::string language = m_conf.getVoiceLanguage();
			std::string directory = m_conf.getVoiceDirectory();

			voice = new CDynVoice(directory, language, m_repeater->getId(), (*it).m_slot, (*it).m_toTG);
			bool ret = voice->open();
			if (!ret) {
				delete voice;
				voice = NULL;
			} else {
				m_dynVoices.push_back(voice);
			}
		}

		CRewriteDynTGNet* netRewriteDynTG = new CRewriteDynTGNet(m_dmr6Name, (*it).m_slot, (*it).m_toTG);
		CRewriteDynTGRF* rfRewriteDynTG = new CRewriteDynTGRF(m_dmr6Name, (*it).m_slot, (*it).m_fromTG, (*it).m_toTG, (*it).m_discPC, (*it).m_statusPC, (*it).m_range, (*it).m_exclTGs, netRewriteDynTG, voice);

		m_dmr6RFRewrites.push_back(rfRewriteDynTG);
		m_dmr6NetRewrites.push_back(netRewriteDynTG);
		m_dynRF.push_back(rfRewriteDynTG);
	}

	std::vector<CIdRewriteStruct> idRewrites = m_conf.getDMRNetwork6IdRewrites();
	for (std::vector<CIdRewriteStruct>::const_iterator it = idRewrites.begin(); it != idRewrites.end(); ++it) {
		LogInfo("    Rewrite Id: %u <-> %u", (*it).m_rfId, (*it).m_netId);

		CRewriteSrcId* rewriteSrcId = new CRewriteSrcId(m_dmr6Name, (*it).m_rfId, (*it).m_netId);
		CRewriteDstId* rewriteDstId = new CRewriteDstId(m_dmr6Name, (*it).m_netId, (*it).m_rfId);

		m_dmr6SrcRewrites.push_back(rewriteSrcId);
		m_dmr6NetRewrites.push_back(rewriteDstId);
	}

	std::vector<unsigned int> tgPassAll = m_conf.getDMRNetwork6PassAllTG();
	for (std::vector<unsigned int>::const_iterator it = tgPassAll.begin(); it != tgPassAll.end(); ++it) {
		LogInfo("    Pass All TG: %u", *it);

		CPassAllTG* rfPassAllTG = new CPassAllTG(m_dmr6Name, *it);
		CPassAllTG* netPassAllTG = new CPassAllTG(m_dmr6Name, *it);

		m_dmr6Passalls.push_back(rfPassAllTG);
		m_dmr6NetRewrites.push_back(netPassAllTG);
	}

	std::vector<unsigned int> pcPassAll = m_conf.getDMRNetwork6PassAllPC();
	for (std::vector<unsigned int>::const_iterator it = pcPassAll.begin(); it != pcPassAll.end(); ++it) {
		LogInfo("    Pass All PC: %u", *it);

		CPassAllPC* rfPassAllPC = new CPassAllPC(m_dmr6Name, *it);
		CPassAllPC* netPassAllPC = new CPassAllPC(m_dmr6Name, *it);

		m_dmr6Passalls.push_back(rfPassAllPC);
		m_dmr6NetRewrites.push_back(netPassAllPC);
	}

	return true;
}



//////////////////////////////////////
bool CDMRGateway::createXLXNetwork()
{
	std::string fileName    = m_conf.getXLXNetworkFile();
    unsigned int reloadTime = m_conf.getXLXNetworkReloadTime();

	m_xlxReflectors = new CReflectors(fileName, reloadTime);

	bool ret = m_xlxReflectors->load();
	if (!ret) {
		delete m_xlxReflectors;
		return false;
	}

	m_xlxLocal         = m_conf.getXLXNetworkLocal();
    m_xlxPort          = m_conf.getXLXNetworkPort();
    m_xlxPassword      = m_conf.getXLXNetworkPassword();
    m_xlxId            = m_conf.getXLXNetworkId();
	m_xlxDebug         = m_conf.getXLXNetworkDebug();
    m_xlxUserControl   = m_conf.getXLXNetworkUserControl();

	if (m_xlxId == 0U)
		m_xlxId = m_repeater->getId();

	m_xlxSlot    = m_conf.getXLXNetworkSlot();
	m_xlxTG      = m_conf.getXLXNetworkTG();
	m_xlxBase    = m_conf.getXLXNetworkBase();
	m_xlxStartup = m_conf.getXLXNetworkStartup();
    m_xlxModule  = m_conf.getXLXNetworkModule();

	unsigned int xlxRelink  = m_conf.getXLXNetworkRelink();

	LogInfo("XLX Network Parameters");
	LogInfo("    Id: %u", m_xlxId);
	LogInfo("    Hosts file: %s", fileName.c_str());
    LogInfo("    Reload time: %u minutes", reloadTime);
	if (m_xlxLocal > 0U)
		LogInfo("    Local: %u", m_xlxLocal);
	else
		LogInfo("    Local: random");
    LogInfo("    Port: %u", m_xlxPort);
	LogInfo("    Slot: %u", m_xlxSlot);
	LogInfo("    TG: %u", m_xlxTG);
	LogInfo("    Base: %u", m_xlxBase);
	if (m_xlxStartup > 0U)
		LogInfo("    Startup: XLX%03u", m_xlxStartup);
	if (xlxRelink > 0U) {
		m_xlxRelink.setTimeout(xlxRelink * 60U);
		LogInfo("    Relink: %u minutes", xlxRelink);
	} else {
		LogInfo("    Relink: disabled");
	}
	if (m_xlxUserControl) {
        LogInfo("    User Control: enabled");
    } else {
        LogInfo("    User Control: disabled");
    }
    if (m_xlxModule) {
        LogInfo("     Module: %c",m_xlxModule);
    }
    

	if (m_xlxStartup > 0U)
		linkXLX(m_xlxStartup);

	m_rptRewrite = new CRewriteTG("XLX", XLX_SLOT, XLX_TG, m_xlxSlot, m_xlxTG, 1U);
	m_xlxRewrite = new CRewriteTG("XLX", m_xlxSlot, m_xlxTG, XLX_SLOT, XLX_TG, 1U);

	return true;
}

bool CDMRGateway::createDynamicTGControl()
{
	unsigned int port = m_conf.getDynamicTGControlPort();

	m_socket = new CUDPSocket(port);

	bool ret = m_socket->open();
	if (!ret) {
		delete m_socket;
		m_socket = NULL;
		return false;
	}

	return true;
}

bool CDMRGateway::linkXLX(unsigned int number)
{
	CReflector* reflector = m_xlxReflectors->find(number);
	if (reflector == NULL)
		return false;

	if (m_xlxNetwork != NULL) {
		LogMessage("XLX, Disconnecting from XLX%03u", m_xlxNumber);
		m_xlxNetwork->close();
		delete m_xlxNetwork;
	}

	m_xlxConnected = false;
	m_xlxRelink.stop();

	m_xlxNetwork = new CDMRNetwork(reflector->m_address, m_xlxPort, m_xlxLocal, m_xlxId, m_xlxPassword, "XLX", VERSION, m_xlxDebug);

	unsigned char config[400U];
	unsigned int len = getConfig("XLX", config);

	m_xlxNetwork->setConfig(config, len);

	bool ret = m_xlxNetwork->open();
	if (!ret) {
		delete m_xlxNetwork;
		m_xlxNetwork = NULL;
		return false;
	}

	m_xlxNumber    = number;
    if (m_xlxModule) {
        m_xlxRoom  = ((int(m_xlxModule) - 64U) + 4000U);
    } else {
        m_xlxRoom      = reflector->m_startup;
    }
	m_xlxReflector = 4000U;

	LogMessage("XLX, Connecting to XLX%03u", m_xlxNumber);

	return true;
}

void CDMRGateway::unlinkXLX()
{
	if (m_xlxNetwork != NULL) {
		m_xlxNetwork->close();
		delete m_xlxNetwork;
		m_xlxNetwork = NULL;
	}

	m_xlxConnected = false;
	m_xlxRelink.stop();
}

void CDMRGateway::writeXLXLink(unsigned int srcId, unsigned int dstId, CDMRNetwork* network)
{
	assert(network != NULL);

	unsigned int streamId = ::rand() + 1U;

	CDMRData data;

	data.setSlotNo(XLX_SLOT);
	data.setFLCO(FLCO_USER_USER);
	data.setSrcId(srcId);
	data.setDstId(dstId);
	data.setDataType(DT_VOICE_LC_HEADER);
	data.setN(0U);
	data.setStreamId(streamId);

	unsigned char buffer[DMR_FRAME_LENGTH_BYTES];

	CDMRLC lc;
	lc.setSrcId(srcId);
	lc.setDstId(dstId);
	lc.setFLCO(FLCO_USER_USER);

	CDMRFullLC fullLC;
	fullLC.encode(lc, buffer, DT_VOICE_LC_HEADER);

	CDMRSlotType slotType;
	slotType.setColorCode(COLOR_CODE);
	slotType.setDataType(DT_VOICE_LC_HEADER);
	slotType.getData(buffer);

	CSync::addDMRDataSync(buffer, true);

	data.setData(buffer);

	for (unsigned int i = 0U; i < 3U; i++) {
		data.setSeqNo(i);
		network->write(data);
	}

	data.setDataType(DT_TERMINATOR_WITH_LC);

	fullLC.encode(lc, buffer, DT_TERMINATOR_WITH_LC);

	slotType.setDataType(DT_TERMINATOR_WITH_LC);
	slotType.getData(buffer);

	data.setData(buffer);

	for (unsigned int i = 0U; i < 2U; i++) {
		data.setSeqNo(i + 3U);
		network->write(data);
	}
}

bool CDMRGateway::rewrite(std::vector<CRewrite*>& rewrites, CDMRData & data, bool trace)
{
	for (std::vector<CRewrite*>::iterator it = rewrites.begin(); it != rewrites.end(); ++it)
		if ((*it)->process(data, trace))
			return true;
	return false;
}

unsigned int CDMRGateway::getConfig(const std::string& name, unsigned char* buffer)
{
	assert(buffer != NULL);

	bool enabled = m_conf.getInfoEnabled();

	if (!enabled) {
		LogInfo("%s: Using original configuration message: %*s", name.c_str(), m_configLen, m_config);
		::memcpy(buffer, m_config, m_configLen);
		return m_configLen;
	}

	LogInfo("%s: Original configuration message: %*s", name.c_str(), m_configLen, m_config);

	char latitude[20U];
	float lat = m_conf.getInfoLatitude();
	::sprintf(latitude, "%08f", lat);

	char longitude[20U];
	float lon = m_conf.getInfoLongitude();
	::sprintf(longitude, "%09f", lon);

	unsigned int power = m_conf.getInfoPower();
	if (power > 99U)
		power = 99U;

	int height = m_conf.getInfoHeight();
	if (height > 999)
		height = 999;

	unsigned int rxFrequency = m_conf.getInfoRXFrequency();
	unsigned int txFrequency = m_conf.getInfoTXFrequency();
	std::string location     = m_conf.getInfoLocation();
	std::string description  = m_conf.getInfoDescription();
	std::string url          = m_conf.getInfoURL();

	::sprintf((char*)buffer, "%-8.8s%09u%09u%02u%2.2s%8.8s%9.9s%03d%-20.20s%-19.19s%c%-124.124s%-40.40s%-40.40s", m_config + 0U,
		rxFrequency, txFrequency, power, m_config + 28U, latitude, longitude, height, location.c_str(),
		description.c_str(), m_config[89U], url.c_str(), m_config + 214U, m_config + 254U);

	LogInfo("%s: New configuration message: %s", name.c_str(), buffer);

	return (unsigned int)::strlen((char*)buffer);
}

void CDMRGateway::processRadioPosition()
{
	unsigned char buffer[50U];
	unsigned int length;
	bool ret = m_repeater->readRadioPosition(buffer, length);
	if (!ret)
		return;

	if (m_xlxNetwork != NULL && (m_status[1U] == DMRGWS_XLXREFLECTOR || m_status[2U] == DMRGWS_XLXREFLECTOR))
		m_xlxNetwork->writeRadioPosition(buffer, length);

	if (m_dmrNetwork1 != NULL && (m_status[1U] == DMRGWS_DMRNETWORK1 || m_status[2U] == DMRGWS_DMRNETWORK1))
		m_dmrNetwork1->writeRadioPosition(buffer, length);

	if (m_dmrNetwork2 != NULL && (m_status[1U] == DMRGWS_DMRNETWORK2 || m_status[2U] == DMRGWS_DMRNETWORK2))
		m_dmrNetwork2->writeRadioPosition(buffer, length);

	if (m_dmrNetwork3 != NULL && (m_status[1U] == DMRGWS_DMRNETWORK3 || m_status[2U] == DMRGWS_DMRNETWORK3))
		m_dmrNetwork3->writeRadioPosition(buffer, length);

	if (m_dmrNetwork4 != NULL && (m_status[1U] == DMRGWS_DMRNETWORK4 || m_status[2U] == DMRGWS_DMRNETWORK4))
		m_dmrNetwork4->writeRadioPosition(buffer, length);

	if (m_dmrNetwork5 != NULL && (m_status[1U] == DMRGWS_DMRNETWORK5 || m_status[2U] == DMRGWS_DMRNETWORK5))
		m_dmrNetwork5->writeRadioPosition(buffer, length);

	if (m_dmrNetwork6 != NULL && (m_status[1U] == DMRGWS_DMRNETWORK6 || m_status[2U] == DMRGWS_DMRNETWORK6))
		m_dmrNetwork6->writeRadioPosition(buffer, length);
}

void CDMRGateway::processTalkerAlias()
{
	unsigned char buffer[50U];
	unsigned int length;
	bool ret = m_repeater->readTalkerAlias(buffer, length);
	if (!ret)
		return;

	if (m_xlxNetwork != NULL && (m_status[1U] == DMRGWS_XLXREFLECTOR || m_status[2U] == DMRGWS_XLXREFLECTOR))
		m_xlxNetwork->writeTalkerAlias(buffer, length);

	if (m_dmrNetwork1 != NULL && (m_status[1U] == DMRGWS_DMRNETWORK1 || m_status[2U] == DMRGWS_DMRNETWORK1))
		m_dmrNetwork1->writeTalkerAlias(buffer, length);

	if (m_dmrNetwork2 != NULL && (m_status[1U] == DMRGWS_DMRNETWORK2 || m_status[2U] == DMRGWS_DMRNETWORK2))
		m_dmrNetwork2->writeTalkerAlias(buffer, length);

	if (m_dmrNetwork3 != NULL && (m_status[1U] == DMRGWS_DMRNETWORK3 || m_status[2U] == DMRGWS_DMRNETWORK3))
		m_dmrNetwork3->writeTalkerAlias(buffer, length);

	if (m_dmrNetwork4 != NULL && (m_status[1U] == DMRGWS_DMRNETWORK4 || m_status[2U] == DMRGWS_DMRNETWORK4))
		m_dmrNetwork4->writeTalkerAlias(buffer, length);

	if (m_dmrNetwork5 != NULL && (m_status[1U] == DMRGWS_DMRNETWORK5 || m_status[2U] == DMRGWS_DMRNETWORK5))
		m_dmrNetwork5->writeTalkerAlias(buffer, length);

	if (m_dmrNetwork6 != NULL && (m_status[1U] == DMRGWS_DMRNETWORK6 || m_status[2U] == DMRGWS_DMRNETWORK6))
		m_dmrNetwork6->writeTalkerAlias(buffer, length);
}

void CDMRGateway::processHomePosition()
{
	unsigned char buffer[50U];
	unsigned int length;
	bool ret = m_repeater->readHomePosition(buffer, length);
	if (!ret)
		return;

	if (m_xlxNetwork != NULL)
		m_xlxNetwork->writeHomePosition(buffer, length);

	if (m_dmrNetwork1 != NULL)
		m_dmrNetwork1->writeHomePosition(buffer, length);

	if (m_dmrNetwork2 != NULL)
		m_dmrNetwork2->writeHomePosition(buffer, length);

	if (m_dmrNetwork3 != NULL)
		m_dmrNetwork3->writeHomePosition(buffer, length);

	if (m_dmrNetwork4 != NULL)
		m_dmrNetwork4->writeHomePosition(buffer, length);

	if (m_dmrNetwork5 != NULL)
		m_dmrNetwork5->writeHomePosition(buffer, length);

	if (m_dmrNetwork6 != NULL)
		m_dmrNetwork6->writeHomePosition(buffer, length);
}

void CDMRGateway::processDynamicTGControl()
{
	unsigned char buffer[100U];
	in_addr address;
	unsigned int port;

	int len = m_socket->read(buffer, 100U, address, port);
	if (len <= 0)
		return;

	buffer[len] = '\0';

	if (::memcmp(buffer + 0U, "DynTG", 5U) == 0) {
		char* pSlot = ::strtok((char*)(buffer + 5U), ", \r\n");
		char* pTG   = ::strtok(NULL, ", \r\n");

		if (pSlot == NULL || pTG == NULL) {
			LogWarning("Malformed dynamic TG control message");
			return;
		}

		unsigned int slot = (unsigned int)::atoi(pSlot);
		unsigned int tg   = (unsigned int)::atoi(pTG);

		for (std::vector<CRewriteDynTGRF*>::iterator it = m_dynRF.begin(); it != m_dynRF.end(); ++it)
			(*it)->tgChange(slot, tg);
	} else {
		LogWarning("Unknown dynamic TG control message: %s", buffer);
	}
}
void ClearNetworks()
{
net1ok=false;
net2ok=false;
net3ok=false;
net4ok=false;
net5ok=false;
net6ok=false;
}


