/*******************************************************************************
 * Copyright (C) 2004-2012 Intel Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  - Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 *  - Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 *  - Neither the name of Intel Corporation. nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL Intel Corporation. OR THE CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *******************************************************************************/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cerrno>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdint.h>
#include <aio.h>
#include "MEILinux.h"

#pragma pack(1)

/*
 * IOCTL Connect Client Data structure
 */
struct mei_connect_client_data {
	union {
		GUID in_client_uuid;
		MEI_CLIENT out_client_properties;
	} d;
} ;
#pragma pack(0)


/* IOCTL commands */
#undef MEI_IOCTL
#undef IOCTL_MEI_CONNECT_CLIENT
#define MEI_IOCTL_TYPE 0x48
#define IOCTL_MEI_CONNECT_CLIENT \
    _IOWR(MEI_IOCTL_TYPE, 0x01, mei_connect_client_data)


#pragma pack(0)


/***************************** public functions *****************************/

MEILinux::MEILinux(const GUID guid, bool verbose) :
MEI(guid, verbose),
_fd(-1)
{
}

MEILinux::~MEILinux()
{
	if (_fd != -1) {
		close(_fd);
	}
}


bool MEILinux::Init(unsigned char reqProtocolVersion)
{
	int result;
	MEI_CLIENT *mei_client;
	bool return_result = true;
	mei_connect_client_data data;

	if (_initialized) {
		Deinit();
	}

	_fd = open("/dev/mei", O_RDWR);

	if (_fd == -1 ) {
		if (_verbose) {
			fprintf(stderr, "Error: Cannot establish a handle to the MEI driver\n");
		}
		return false;
	}
	_initialized = true;


	if (_verbose) 
		fprintf(stdout, "Connected to MEI driver\n");

	memset(&data, 0, sizeof(data));

	memcpy(&data.d.in_client_uuid, &_guid, sizeof(_guid));
	result = ioctl(_fd, IOCTL_MEI_CONNECT_CLIENT, &data);
	if (result) {
		if (_verbose) {
			fprintf(stderr, "error in IOCTL_MEI_CONNECT_CLIENT receive message. err=%d\n", result);
		}
		return_result = false;
		Deinit();
		goto mei_free;
	}
	mei_client = &data.d.out_client_properties;
	if (_verbose) {
		fprintf(stdout, "max_message_length %d \n", (mei_client->MaxMessageLength));
		fprintf(stdout, "protocol_version %d \n", (mei_client->ProtocolVersion));
	}

	if ((reqProtocolVersion > 0) && (mei_client->ProtocolVersion != reqProtocolVersion)) {
		if (_verbose) {
			fprintf(stderr, "Error: MEI protocol version not supported\n");
		}
		return_result = false;
		Deinit();
		goto mei_free;
	}

	_protocolVersion = mei_client->ProtocolVersion;
	_bufSize = mei_client->MaxMessageLength;

mei_free:

	return return_result;
}

void MEILinux::Deinit()
{
	if (_fd != -1) {
		close(_fd);
		_fd = -1;
	}

	_bufSize = 0;
	_protocolVersion = 0;
	_initialized = false;
}

int MEILinux::ReceiveMessage(unsigned char *buffer, int len, unsigned long timeout)
{
	int rv = 0;
	int error = 0;

	if (_verbose) {
		fprintf(stdout, "call read length = %d\n", len);
	}
	rv = read(_fd, (void*)buffer, len);
	if (rv < 0) {
		error = errno;
		if (_verbose) {
			fprintf(stderr, "read failed with status %d %d\n", rv, error);
		}
		Deinit();
	} else {
		if (_verbose) {
			fprintf(stderr, "read succeeded with result %d\n", rv);
		}
	}
	return rv;
}

int MEILinux::SendMessage(const unsigned char *buffer, int len, unsigned long timeout)
{
	int rv = 0;
	int return_length =0;
	int error = 0;
	fd_set set;
	struct timeval tv;

	tv.tv_sec =  timeout / 1000;
	tv.tv_usec =(timeout % 1000) * 1000000;

	if (_verbose) {
		fprintf(stdout, "call write length = %d\n", len);
	}
	rv = write(_fd, (void *)buffer, len);
	if (rv < 0) {
		error = errno;
		if (_verbose) {
			fprintf(stderr,"write failed with status %d %d\n", rv, error);
		}
		goto out;
	}

	return_length = rv;

	FD_ZERO(&set);
	FD_SET(_fd, &set);
	rv = select(_fd+1 ,&set, NULL, NULL, &tv);
	if (rv > 0 && FD_ISSET(_fd, &set)) {
		if (_verbose) {
			fprintf(stderr, "write success\n");
		}
	}
	else if (rv == 0) {
		if (_verbose) {
			fprintf(stderr, "write failed on timeout with status\n");
		}
		goto out;
	}
	else { //rv<0
		if (_verbose) {
			fprintf(stderr, "write failed on select with status %d\n", rv);
		}
		goto out;
	}

	rv = return_length;

out:
	if (rv < 0) {
		Deinit();
	}

	return rv;
}

