/*
		MIT License
  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  THE SOFTWARE.
*/

#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <errno.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include "ota.h"
#include "cJSON.h"




#define OTA_PORT	(12345)
#define BACKLOG	(10)



static void *otaThread(void *params)
{
	
	OTANet *ota = (OTANet *)params;
	if(ota)
	{
		ota->run();
	}
	
	return nullptr;
}




OTANet::OTANet()
{
	b_reboot = false;
	m_socket = -1;
	memset(crc_tab, 0, sizeof(crc_tab));
	memset(&m_info, 0, sizeof(m_info));
	m_sta = OTA_STA_IDLE;
	makeCrcTab();
	m_image = "/tmp/ota.img";
	delImg();
	

}

OTANet::~OTANet()
{
	if (m_socket > 0)
	{
		close(m_socket);
		m_socket = -1;
	}

}

void OTANet::init()
{
	m_socket = socket(AF_INET, SOCK_STREAM, 0);
	if (m_socket < 0)
	{
		perror("kOTA socket create error.");
		return;
	}
	struct sockaddr_in s_sockaddr;
	s_sockaddr.sin_family = AF_INET;
	s_sockaddr.sin_port = htons(OTA_PORT);
	s_sockaddr.sin_addr.s_addr = INADDR_ANY;
	bzero(&(s_sockaddr.sin_zero), 8);
	if (bind(m_socket, (struct sockaddr *)&s_sockaddr, sizeof(struct sockaddr)) < 0)
	{
		perror("OTA bind socket error.");
		close(m_socket);
		return;
	}
	if (listen(m_socket, BACKLOG) < 0)
	{
		perror("OTA listen socket error.");
		close(m_socket);
		return;
	}
	
	pthread_t pid;
	if (pthread_create(&pid, nullptr, otaThread, this) != 0)
	{
		perror("pthread_create otaThread!\n");
	}

	
}

void OTANet::run(void)
{
	if (m_socket < 0)
	{
		fprintf(stderr, "OTA socket doesn't initing.\n");
		return;
	}
	int client_fd;
	int ret;
	socklen_t sin_size;
	while (1)
	{
		struct timeval tv;
        fd_set rfs;
        FD_ZERO(&rfs);
        FD_SET(m_socket, &rfs);
        tv.tv_sec = 0;
        tv.tv_usec = 400000;
        select(m_socket+1, &rfs, NULL, NULL, &tv);

        if (FD_ISSET(m_socket, &rfs)) 
		{
			client_fd = accept(m_socket, NULL, &sin_size);
			if (client_fd > 0)
			{
				
				m_sta = OTA_STA_RECVING_HEADER;
				if (recvHeader(client_fd) != 0)
				{
					ackSta(client_fd, 0, OTA_STA_INVALID_HEADER);
					m_sta = OTA_STA_IDLE;
					close(client_fd);
					
					continue;
				}
				ackSta(client_fd, 0, OTA_STA_RECVING_IMG);
				m_sta = OTA_STA_RECVING_IMG;
				ret = recvImg(client_fd);
				if (ret != 0)
				{
					ackSta(client_fd, 0, OTA_STA_RECV_ERR);
					m_sta = OTA_STA_IDLE;
					close(client_fd);
					
					continue;
				}
				m_sta = OTA_STA_BURNNING;
				ret = burnImg(client_fd);
				delImg();
				if (ret == 0)
				{
					m_sta = OTA_STA_FINISH;
					ackSta(client_fd, 0, OTA_STA_FINISH);
					usleep(1000*20);
					close(client_fd);
					m_sta = OTA_STA_IDLE;		
					break;
				}else{
					m_sta = OTA_STA_FINISH;
					ackSta(client_fd, 0, ret);
					m_sta = OTA_STA_IDLE;
					
				}
			}
		}
	}
	
}




void OTANet::makeCrcTab(void)
{
	uint32_t c;
	int bit = 0;
	
	for (int i = 0; i < 256; i++)
	{
		c = (uint32_t)i;
		for (bit = 0; bit < 8;bit++)
		{
			if (c&1)
			{
				c = (c >> 1)^(0xEDB88320);
			}else{
				c = c >> 1;
			}
		}
		crc_tab[i] = c;
	}
}

uint32_t OTANet::makeCrc(uint32_t crc, uint8_t *source, uint32_t size)
{
	while(size --)
	{
		crc = (crc >> 8)^(crc_tab[(crc ^ (*source++))&0xFF]);
	}
	return crc;
}

uint32_t OTANet::makeFileCrc(const char *file)
{
	uint32_t crc = 0;
	uint32_t count = 0;
	if (!file)
	{
		return crc;
	}
	uint8_t buf[1024] = {0};
	struct stat st;
	if (0 != stat(file, &st))
	{
		return crc;
	}
	FILE *fp = fopen(file, "rb");
	if (!fp)
	{
		return crc;
	}
	while (!feof(fp))
	{
		memset(buf, 0, 1024);
		count = fread(buf, 1, sizeof(buf), fp);
		crc = makeCrc(crc, buf, count);
	}
	fclose(fp);
	
	return crc;
}

void OTANet::ackSta(int client_fd, uint32_t value, int sta)
{

	cJSON *root = cJSON_CreateObject();
	cJSON_AddNumberToObject(root, "status", sta);
	cJSON_AddNumberToObject(root, "value", value);
	char *str = cJSON_PrintUnformatted(root);
	if(str)
	{
		if (send(client_fd, str, strlen(str),0) <= 0)
		{
			fprintf(stderr, "sent msg failed to server.\n");
		}		
		cJSON_free(str);
   		str = NULL;
	}
	cJSON_Delete(root);

}

int OTANet::recvHeader(int client_fd)
{
	struct timeval tv;
	fd_set rfs;
	FD_ZERO(&rfs);
	FD_SET(client_fd, &rfs);
	tv.tv_sec = 5;
	tv.tv_usec = 0;
	select(client_fd+1, &rfs, NULL, NULL, &tv);
	int ret;
	if (FD_ISSET(client_fd, &rfs)) 
	{
		ret = recv(client_fd, &m_info, sizeof(m_info), 0);

		
		if (m_info.length <= 0)
		{
			fprintf(stderr, "invalid length.\n");
			return -1;
		}
		if (m_info.crc32 == 0)
		{
			fprintf(stderr, "crc value is invalid.\n");
			return -1;
		}
		return 0;
		
	}
	fprintf(stderr, "recv header timeout.\n");
	return -1;
}

int OTANet::recvImg(int client_fd)
{
	
	fprintf(stderr, "start recv img length:%d\n", m_info.length);	
	delImg();
	
	char *cache = new char[1024*64];
	memset(cache, 0, 1024*64);
	FILE *fw = fopen(m_image.c_str(), "wb");
	if (!fw)
	{
		fprintf(stderr, "can't create img file.\n");
		delete []cache;
		cache = NULL;
		return -2;
	}
	int recv_err = 0;
	size_t recv_size =0;
	uint32_t recv_total = 0;
	uint32_t per = 0, last_per = 0;
	while (recv_err < 10)
	{
		struct timeval tv;
		fd_set rfs;
		FD_ZERO(&rfs);
		FD_SET(client_fd, &rfs);
		tv.tv_sec = 1;
		tv.tv_usec = 0;
		memset(cache, 0, 1024*64);
		
		select(client_fd+1, &rfs, NULL, NULL, &tv);
		if (FD_ISSET(client_fd, &rfs)) 
		{
			recv_size = recv(client_fd, cache, 1024*64, 0);
			if (recv_size > 0)
			{
				
				fwrite(cache, 1, recv_size, fw);
				recv_total += recv_size;
				per = (uint32_t)(recv_total*100/m_info.length);
				
				if (per - last_per >= 10)
				{
					ackSta(client_fd, per, OTA_STA_RECVING_IMG);
					last_per = per;
				}

				if (recv_total == m_info.length)
				{
					break;
				}else{
					continue;					
				}

			}else{
				recv_err++;
			}
		}else{
			recv_err++;			
		}
	
	}
	
	if (recv_total != m_info.length)
	{
		fprintf(stderr, "can't recv complete img.\n");
		fclose(fw);
		delete []cache;
		cache = NULL;
		return -2;
	}
	
	
	system("sync");
	fclose(fw);
	delete []cache;
	cache = NULL;
	return 0;

}

int OTANet::burnImg(int client_fd)
{
	
	struct stat st;
	if (0 != stat(m_image.c_str(), &st))
	{
		fprintf(stderr, "img does not exist.\n");
		return OTA_STA_BURNNING_ERR;
	}
	uint32_t crc = makeFileCrc(m_image.c_str());
	if (m_info.crc32 != crc)
	{
		fprintf(stderr, "image crc check failed source:%x target:%x\n",
			m_info.crc32, crc);
		return OTA_STA_CRC_ERR;
	}
	
	/*
		Warnning:
		
		nand needs to be modified to the corresponding 
		partition name according to the actual device
	*/
	const char *nand = "/dev/mtdb05";
	char cmd[256] = {0};
	sprintf(cmd, "busybox dd if=%s of=%s", m_image.c_str(), nand);
	ackSta(client_fd, 0, OTA_STA_BURNNING);
	system(cmd);
	ackSta(client_fd, 100, OTA_STA_BURNNING);
	return 0;

	
}

void OTANet::delImg(void)
{
	
}

