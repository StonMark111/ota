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
#ifndef __OTA_H__
#define __OTA_H__
#include <stdint.h>
#include <unistd.h>
#include <string>
#include <memory>

enum{
	OTA_STA_CRC_ERR = -5,
	OTA_STA_PARTITION_ERR = -4,
	OTA_STA_BURNNING_ERR = -3,
	OTA_STA_RECV_ERR = -2,
	OTA_STA_INVALID_HEADER = -1,
	OTA_STA_IDLE = 0,
	OTA_STA_RECVING_HEADER,
	OTA_STA_RECVING_IMG,
	OTA_STA_BURNNING,
	OTA_STA_FINISH,
	OTA_STA_REBOOT,
};

typedef struct {	
    uint32_t length;			//4	 Byte
    uint32_t crc32;				//4	 Byte
	uint8_t reserve[16];		//16 Byte
} image_header_t;

class OTANet
{
public:
	OTANet();
	~OTANet();
	void init();
	void run(void);
	bool busying();
public:
	bool b_reboot;

private:
	void makeCrcTab(void);
	uint32_t makeCrc(uint32_t crc, uint8_t *source, uint32_t size);
	uint32_t makeFileCrc(const char *file);
	void ackSta(int client_fd, uint32_t value, int sta);
	int recvHeader(int client_fd);
	int recvImg(int client_fd);
	int burnImg(int client_fd);
	void delImg(void);

private:
	int m_socket;
	uint32_t crc_tab[256];
	uint32_t m_sta;
	image_header_t m_info;
	std::string m_image;
};


#endif