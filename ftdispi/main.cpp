#include <iostream>
#include <iterator>
#include <fstream>
#include <algorithm>
#include <cstdlib>
#include <cstdio>

using namespace std;

#include "ftd2xx.h"
#include <windows.h>

#include <boost/program_options.hpp>

using namespace boost;
namespace po = boost::program_options;

FT_STATUS ftStatus; //Handle to all FTDI requests

bool verbose;

//this routine is used to initial SPI interface
BOOL SPI_Initial(FT_HANDLE ftHandle)
{
	DWORD dwCount;
	DWORD inputBuffer = 0;
	DWORD bytesSent = 0;
	DWORD bytesRead = 0;
	DWORD bytesToSend = 0; //Index of output buffer

	BYTE tmpBuffer[16];
	BYTE InputBuffer[16]; 

	ftStatus = FT_ResetDevice(ftHandle); //Reset USB device
										 //Purge USB receive buffer first by reading out all old data from FT2232Hreceive buffer
	ftStatus |= FT_GetQueueStatus(ftHandle, &inputBuffer); // Get the	number of bytes in the FT2232H receive buffer

	if ((ftStatus == FT_OK) && (inputBuffer > 0))
		ftStatus |= FT_Read(ftHandle, InputBuffer, inputBuffer, &bytesRead); //Read out the data from FT2232H receive buffer

	ftStatus |= FT_SetUSBParameters(ftHandle, 65535, 65535); //Set USB request transfer size
	ftStatus |= FT_SetChars(ftHandle, false, 0, false, 0); //Disable event and error characters
	ftStatus |= FT_SetTimeouts(ftHandle, 3000, 3000); //Sets the read and write timeouts in 3 sec for the FT2232H
	ftStatus |= FT_SetLatencyTimer(ftHandle, 50); //Set the latency timer 1ms
	ftStatus |= FT_SetBitMode(ftHandle, 0x0, 0x00); //Reset	controller
	ftStatus |= FT_SetBitMode(ftHandle, 0x0, 0x02); //Enable MPSSE mode

	if (ftStatus != FT_OK)
	{
		if (verbose) cout << "fail on initialize FT2232H device ! \n";
		return false;
	}

	Sleep(50); // Wait 50ms for all the USB stuff to complete and work
			   //////////////////////////////////////////////////////////////////
			   // Synchronize the MPSSE interface by sending bad commandn xAA
			   //////////////////////////////////////////////////////////////////

	bytesToSend = 0; //Clear output buffer
	tmpBuffer[bytesToSend++] = '\xAA'; //Add BAD command
	ftStatus = FT_Write(ftHandle, tmpBuffer, bytesToSend, &bytesSent); // Send off the BAD commands

	do
	{
		ftStatus = FT_GetQueueStatus(ftHandle, &inputBuffer); // Get the number of bytes in the device input buffer
	} while ((inputBuffer == 0) && (ftStatus == FT_OK)); //or Timeout

	bool bCommandEchod = false;

	ftStatus = FT_Read(ftHandle, InputBuffer, inputBuffer, &bytesRead); //Read out the data from input buffer

	for (dwCount = 0; dwCount < (bytesRead - 1); dwCount++) //Checkif Bad command and echo command received
	{
		if ((InputBuffer[dwCount] == BYTE('\xFA')) && (InputBuffer[dwCount + 1] == BYTE('\xAA')))
		{
			bCommandEchod = true;
			break;
		}
	}

	if (bCommandEchod == false)
	{
		if (verbose) cout << "fail to synchronize MPSSE with command '0xAA' \n";
		return false; /*Error, can't receive echo command , fail to synchronize MPSSE interface;*/
	}

	//////////////////////////////////////////////////////////////////
	// Synchronize the MPSSE interface by sending bad command xAB
	//////////////////////////////////////////////////////////////////
	bytesToSend = 0;
	tmpBuffer[bytesToSend++] = '\xAB'; //Send BAD command ＆xAB＊
	ftStatus = FT_Write(ftHandle, tmpBuffer, bytesToSend, &bytesSent); // Send off the BAD commands

	

	do
	{
		ftStatus = FT_GetQueueStatus(ftHandle, &inputBuffer); //Getthe number of bytes in the device input buffer
	} while ((inputBuffer == 0) && (ftStatus == FT_OK)); //or Timeout

	bCommandEchod = false;

	ftStatus = FT_Read(ftHandle, InputBuffer, inputBuffer, &bytesRead); //Read out the data from input buffer

	for (dwCount = 0; dwCount < (bytesRead - 1); dwCount++) //Check if Bad command and echo command received
	{
		if ((InputBuffer[dwCount] == BYTE('\xFA')) && (InputBuffer[dwCount + 1] == BYTE('\xAB')))
		{
			bCommandEchod = true;
			break;
		}
	}
	if (bCommandEchod == false)
	{
		if (verbose) cout << "fail to synchronize MPSSE with command '0xAB' \n";
		return false;
		/*Error, can't receive echo command , fail to synchronize MPSSE
		interface;*/
	}


	////////////////////////////////////////////////////////////////////
	//Configure the MPSSE for SPI communication with EEPROM
	//////////////////////////////////////////////////////////////////
	
	bytesToSend = 0;
	tmpBuffer[bytesToSend++] = 0x8A; //Ensure disable clock divide by 5 for 60Mhz master clock
	tmpBuffer[bytesToSend++] = 0x97; //Ensure turn off adaptive clocking
	tmpBuffer[bytesToSend++] = 0x8D; //disable 3 phase data clock ftStatus = FT_Write(ftHandle, tmpBuffer, bytesToSend,
	ftStatus = FT_Write(ftHandle, tmpBuffer, bytesToSend, &bytesSent); // Send out the commands

	bytesToSend = 0;
	tmpBuffer[bytesToSend++] = 0x80; //Command to set directions of lower 8 pins and force value on bits set as output
	tmpBuffer[bytesToSend++] = 0x00; //Set SDA, SCL high, WP disabled by SK, DO at bit ＆＊, GPIOL0 at bit ＆＊
	tmpBuffer[bytesToSend++] = 0x0b; //Set SK,DO,GPIOL0 pins as output with bit ＊＊, other pins as input with bit ＆＊
											 //The SK clock frequency can be worked out by below algorithm with divide					by 5 set as off
											 //SK frequency = 60MHz /((1 + [(1 +0xValueH*256) OR 0xValueL])*2)
	tmpBuffer[bytesToSend++] = 0x86; //Command to set clock divisor

	//SCL Frequency =60 / ((1 + 29) * 2) (MHz) = 1Mhz
	tmpBuffer[bytesToSend++] = BYTE(29 & '\xFF'); //Set 0xValueL of clock divisor
	tmpBuffer[bytesToSend++] = BYTE(29 >> 8); //Set 0xValueH of clock divisor

	ftStatus = FT_Write(ftHandle, tmpBuffer, bytesToSend, &bytesSent); // Send out the commands

	Sleep(20); //Delay for a while
			   //Turn off loop back in case

	bytesToSend = 0;
	tmpBuffer[bytesToSend++] = 0x85; //Command to turn off loop back of TDI / TDO connection
	ftStatus = FT_Write(ftHandle, tmpBuffer, bytesToSend, &bytesSent); //Send out the commands
	bytesToSend = 0; //Clear output buffer

	Sleep(30); //Delay for a while

	if (verbose) cout << "SPI initial successful\n";
	return true;
}

BOOL SPI_WriteNBytes(FT_HANDLE ftHandle, BYTE *bdata, int nbytes, DWORD *dwBytesSent)
{
	BYTE *tmpBuffer = (BYTE *)malloc(nbytes + 33);
	if (tmpBuffer == NULL) return 1;

	//one 0x80 command can keep 0.2us, do 5 times to stay in this situation for 1us
	tmpBuffer[0] = '\x80';//GPIO command for ADBUS
	tmpBuffer[1] = '\x00';//set CS high, MOSI and SCL low
	tmpBuffer[2] = '\x0b';//bit3:CS, bit2:MISO,bit1:MOSI, bit0 : SCK
	tmpBuffer[3] = '\x80';//GPIO command for ADBUS
	tmpBuffer[4] = '\x00';//set CS high, MOSI and SCL low
	tmpBuffer[5] = '\x0b';//bit3:CS, bit2:MISO,bit1:MOSI, bit0 : SCK
	tmpBuffer[6] = '\x80';//GPIO command for ADBUS
	tmpBuffer[7] = '\x00';//set CS high, MOSI and SCL low
	tmpBuffer[8] = '\x0b';//bit3:CS, bit2:MISO,bit1:MOSI, bit0 : SCK
	tmpBuffer[9] = '\x80';//GPIO command for ADBUS
	tmpBuffer[10] = '\x00';//set CS high, MOSI and SCL low
	tmpBuffer[11] = '\x0b';//bit3:CS, bit2:MISO,bit1:MOSI, bit0 : SCK
	tmpBuffer[12] = '\x80';//GPIO command for ADBUS
	tmpBuffer[13] = '\x00';//set CS high, MOSI and SCL low
	tmpBuffer[14] = '\x0b';//bit3:CS, bit2:MISO,bit1:MOSI, bit0 : SCK

	tmpBuffer[15] = 0x11; // MSB_FALLING_EDGE_CLOCK_BYTE_OUT;
	tmpBuffer[16] = (nbytes & 0xFF) - 1;
	tmpBuffer[17] = (nbytes >> 8) & 0xFF;
	memcpy(&tmpBuffer[18], &bdata[0], nbytes * sizeof(BYTE));

	//one 0x80 command can keep 0.2us, do 5 times to stay in this situation for 1us
	tmpBuffer[nbytes + 18] = '\x80';//GPIO command for ADBUS
	tmpBuffer[nbytes + 19] = '\x08';//set CS high, MOSI and SCL low
	tmpBuffer[nbytes + 20] = '\x0b';//bit3:CS, bit2:MISO,bit1:MOSI, bit0 : SCK
	tmpBuffer[nbytes + 21] = '\x80';//GPIO command for ADBUS
	tmpBuffer[nbytes + 22] = '\x08';//set CS high, MOSI and SCL low
	tmpBuffer[nbytes + 23] = '\x0b';//bit3:CS, bit2:MISO,bit1:MOSI, bit0 : SCK
	tmpBuffer[nbytes + 24] = '\x80';//GPIO command for ADBUS
	tmpBuffer[nbytes + 25] = '\x08';//set CS high, MOSI and SCL low
	tmpBuffer[nbytes + 26] = '\x0b';//bit3:CS, bit2:MISO,bit1:MOSI, bit0 : SCK
	tmpBuffer[nbytes + 27] = '\x80';//GPIO command for ADBUS
	tmpBuffer[nbytes + 28] = '\x08';//set CS high, MOSI and SCL low
	tmpBuffer[nbytes + 29] = '\x0b';//bit3:CS, bit2:MISO,bit1:MOSI, bit0 : SCK
	tmpBuffer[nbytes + 30] = '\x80';//GPIO command for ADBUS
	tmpBuffer[nbytes + 31] = '\x08';//set CS high, MOSI and SCL low
	tmpBuffer[nbytes + 32] = '\x0b';//bit3:CS, bit2:MISO,bit1:MOSI, bit0 : SCK

	ftStatus = FT_Write(ftHandle, tmpBuffer, nbytes + 33, dwBytesSent); //send out MPSSE command to MPSSE engine

	free(tmpBuffer);
	return ftStatus;
}

BOOL SPI_ReadNBytes(FT_HANDLE ftHandle, BYTE* bdata, int nbytes, DWORD *dwBytesRead)
{
	WORD tmpBuffer[33];

	//one 0x80 command can keep 0.2us, do 5 times to stay in this situation for 1us
	tmpBuffer[0] = '\x80';//GPIO command for ADBUS
	tmpBuffer[1] = '\x00';//set CS high, MOSI and SCL low
	tmpBuffer[2] = '\x0b';//bit3:CS, bit2:MISO,bit1:MOSI, bit0 : SCK
	tmpBuffer[3] = '\x80';//GPIO command for ADBUS
	tmpBuffer[4] = '\x00';//set CS high, MOSI and SCL low
	tmpBuffer[5] = '\x0b';//bit3:CS, bit2:MISO,bit1:MOSI, bit0 : SCK
	tmpBuffer[6] = '\x80';//GPIO command for ADBUS
	tmpBuffer[7] = '\x00';//set CS high, MOSI and SCL low
	tmpBuffer[8] = '\x0b';//bit3:CS, bit2:MISO,bit1:MOSI, bit0 : SCK
	tmpBuffer[9] = '\x80';//GPIO command for ADBUS
	tmpBuffer[10] = '\x00';//set CS high, MOSI and SCL low
	tmpBuffer[11] = '\x0b';//bit3:CS, bit2:MISO,bit1:MOSI, bit0 : SCK
	tmpBuffer[12] = '\x80';//GPIO command for ADBUS
	tmpBuffer[13] = '\x00';//set CS high, MOSI and SCL low
	tmpBuffer[14] = '\x0b';//bit3:CS, bit2:MISO,bit1:MOSI, bit0 : SCK

	tmpBuffer[15] = 0x24; // MSB_FALLING_EDGE_CLOCK_BYTE_IN;
	tmpBuffer[16] = (nbytes & 0xFF);
	tmpBuffer[17] = (nbytes >> 8) & 0xFF;

	//one 0x80 command can keep 0.2us, do 5 times to stay in this situation for 1us
	tmpBuffer[18] = '\x80';//GPIO command for ADBUS
	tmpBuffer[19] = '\x08';//set CS high, MOSI and SCL low
	tmpBuffer[20] = '\x0b';//bit3:CS, bit2:MISO,bit1:MOSI, bit0 : SCK
	tmpBuffer[21] = '\x80';//GPIO command for ADBUS
	tmpBuffer[22] = '\x08';//set CS high, MOSI and SCL low
	tmpBuffer[23] = '\x0b';//bit3:CS, bit2:MISO,bit1:MOSI, bit0 : SCK
	tmpBuffer[24] = '\x80';//GPIO command for ADBUS
	tmpBuffer[25] = '\x08';//set CS high, MOSI and SCL low
	tmpBuffer[26] = '\x0b';//bit3:CS, bit2:MISO,bit1:MOSI, bit0 : SCK
	tmpBuffer[27] = '\x80';//GPIO command for ADBUS
	tmpBuffer[28] = '\x08';//set CS high, MOSI and SCL low
	tmpBuffer[29] = '\x0b';//bit3:CS, bit2:MISO,bit1:MOSI, bit0 : SCK
	tmpBuffer[30] = '\x80';//GPIO command for ADBUS
	tmpBuffer[31] = '\x08';//set CS high, MOSI and SCL low
	tmpBuffer[32] = '\x0b';//bit3:CS, bit2:MISO,bit1:MOSI, bit0 : SCK

	ftStatus = FT_Write(ftHandle, tmpBuffer, 33, dwBytesRead);
	ftStatus = FT_Read(ftHandle, bdata, nbytes, dwBytesRead);

	return ftStatus;
}

int main(int ac, char *av[])
{
	bool rflag, wflag;
	int nbytes, devnum;
	string inputfilepath, outputfilepath;

	//Try to catch any boost-related exception
	try
	{
		//Creating usage-style options
		po::options_description desc("Usage");
		desc.add_options()
			("help,h", "this message")
			("device,d", po::value<int>(&devnum)->default_value(0)->implicit_value(0), "Select device ID to work with")
			("read,r", po::bool_switch(&rflag), "Read from SPI device flag")
			("write,w", po::bool_switch(&wflag), "Write to SPI device flag")
			("length,l", po::value<int>(&nbytes), "Data length in bytes")
			("input-file,i", po::value<string>(&inputfilepath), "Input data file")
			("output-file,o", po::value<string>(&outputfilepath), "Output data file")
			("verbose,v", po::bool_switch(&verbose), "Turn on verbose output")
			;

		po::variables_map vm;
		po::store(po::command_line_parser(ac, av).options(desc).run(), vm);
		po::notify(vm);

		/*
		Check if all necessary options and switches are provided
		*/
		if (vm.count("help") || ac<2)
		{
			cout << desc << "\n" << "Example usage:" << endl << endl;
			cout << "  Reading 1024 bytes from SPI first (0) device:" << endl;
			cout << "    " << av[0] << " -r -l 1024 -o ./Path/to/file.bin" << endl << endl;
			cout << "  Writing 8 bytes to SPI second (1) device, with verbose program output:" << endl;
			cout << "    " << av[0] << " -v -d 1 -w -l 8 -i inp.bin" << endl;
			return 0;
		}

		if (!rflag && !wflag)
		{
			cerr << "Specify an action (read or write)\n";
			return 1;
		}

		if (rflag && !vm.count("output-file"))
		{
			cerr << "Specify a output file path to perform read operation\n";
			return 1;
		}

		if (wflag && !vm.count("input-file"))
		{
			cerr << "Specify an input file path to perform write operation\n";
			return 1;
		}

		if (!vm.count("length"))
		{
			cerr << "Specify length of a data\n";
			return 1;
		}
	}
	catch (std::exception& e)
	{
		cout << e.what() << "\n";
		return 1;
	}

	/*
	Done checking options, now try to communicate via SPI interface
	*/
	ifstream inputfiledata;
	ofstream outputfiledata;

	if (wflag)
		inputfiledata.open(inputfilepath, ios::binary | ios::in); // Input: get data from file in order to send it via SPI

	if (rflag)
		outputfiledata.open(outputfilepath, ios::binary | ios::out | ios::trunc); // Output: write read data from interface


	FT_HANDLE ftdiHandle;
	DWORD numDevs;
	FT_DEVICE_LIST_INFO_NODE *devInfo;
	ftStatus = FT_CreateDeviceInfoList(&numDevs);

	if (ftStatus == FT_OK)
	{
		if (verbose)
			cout << "Number of devices is " << numDevs << "\n";
	}
	else
	{
		return 1;
	}

	if (numDevs > 0)
	{
		// allocate storage for list based on numDevs
		devInfo = (FT_DEVICE_LIST_INFO_NODE*)malloc(sizeof(FT_DEVICE_LIST_INFO_NODE)*numDevs);

		// get the device information list
		ftStatus = FT_GetDeviceInfoList(devInfo, &numDevs);

		if (ftStatus == FT_OK)
		{
			for (unsigned int i = 0; i < numDevs; i++)
			{
				if (verbose)
					cout << "Dev " << i << ":\n" <<
						" Flags=0x" << hex << devInfo[i].Flags << "\n" <<
						" Type=0x" << devInfo[i].Type << "\n" <<
						" ID=0x" << devInfo[i].ID << "\n" <<
						" LocId=0x" << devInfo[i].LocId << "\n" <<
						" SerialNumber=" << devInfo[i].SerialNumber << "\n" <<
						" Description=" << devInfo[i].Description << "\n" <<
						" ftHandle=0x" << devInfo[i].ftHandle << "\n";
			}
		}
	}
	else return 1;

	ftStatus = FT_Open(devnum, &ftdiHandle);
	if (ftStatus != FT_OK)
	{
		if (verbose) cout << "Can't open FT2232H device!\n";
		return 1;
	}
	else if (ftStatus == FT_OK && verbose) cout << "Successfully open FT2232H device (" << devnum << ")!\n";

	if (SPI_Initial(ftdiHandle) == TRUE)
	{
		int i = 0;

		byte ReadByte = 0;

		//Purge USB received buffer first before read operation
		int inputBufferResidue;
		int bytesRead;
		char tmpBuffer[1024];
		ftStatus = FT_GetQueueStatus(ftdiHandle, &(DWORD)inputBufferResidue); // Get the number of bytes in the device receive buffer
		if ((ftStatus == FT_OK) && (inputBufferResidue > 0))
			FT_Read(ftdiHandle, tmpBuffer, (DWORD)inputBufferResidue, &(DWORD)bytesRead); //Read out all the data from receive buffer


		if (wflag)
		{
			int bytesSent;

			BYTE *tmpBuffer = (BYTE *)malloc(nbytes);

			if (tmpBuffer == NULL)
			{
				if (verbose) cout << "Cannot initialize output buffer";
				return 1;
			}

			inputfiledata.read((char *)tmpBuffer, nbytes);
			SPI_WriteNBytes(ftdiHandle, tmpBuffer, nbytes, &(DWORD)bytesSent);

			if (verbose) {
				cout << "Sent " << dec << bytesSent << " bytes to device " << devnum << " ("<< nbytes <<" bytes of data):\n";
				for (int i = 0; i < nbytes; i++)
					printf("0x%x,", tmpBuffer[i]);
			}

			free(tmpBuffer);
		}

		if (rflag)
		{
			int bytesRead;

			BYTE *tmpBuffer = (BYTE *)malloc(nbytes);

			if (tmpBuffer == NULL)
			{
				if (verbose) cout << "Cannot initialize input buffer";
				return 1;
			}

			SPI_ReadNBytes(ftdiHandle, tmpBuffer, nbytes, &(DWORD)bytesRead);

			if (verbose) {
				cout << "Read " << dec << bytesRead << " bytes from device " << devnum << " (" << nbytes << " bytes of data):\n";
				for (int i = 0; i < nbytes; i++)
					printf("0x%x,", tmpBuffer[i]);
			}

			outputfiledata.write((char *)tmpBuffer, nbytes);

			free(tmpBuffer);
		}
	}

	FT_Close(ftdiHandle);

	inputfiledata.close();
	outputfiledata.close();

	//return app.exec();
	return 0;
}
