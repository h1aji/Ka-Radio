/*
 * audio_player.c
 *
 *  Created on: 12.03.2017
 *      Author: michaelboeckling
 */

#define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE
#define TAG "audio_player"

#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"

#include "audio_player.h"
#include "buffer.h"
#include "main.h"
#include "vs1053.h"
#include "webclient.h"

static player_t *player_instance = NULL;
static component_status_t player_status = UNINITIALIZED;

static int start_decoder_task(player_t *player)
{
    TaskFunction_t task_func;
    char * task_name;
    uint16_t stack_depth;
	int priority = PRIO_MAD;

    ESP_LOGD(TAG, "RAM left %d", esp_get_free_heap_size());

	task_func = vsTask;
	task_name = (char*)"vsTask";
	stack_depth = 3000;
	priority = PRIO_VS1053;

	if (((task_func != NULL)) && (xTaskCreatePinnedToCore(task_func, task_name, stack_depth, player,
			priority, NULL, CPU_MAD) != pdPASS)) 
	{
									
		ESP_LOGE(TAG, "ERROR creating decoder task! Out of memory?");
		spiRamFifoReset();
		return -1;
	} else {
		player->decoder_status = RUNNING;
	}
	
	ESP_LOGD(TAG, "decoder task created: %s", task_name);

    return 0;
}

static int t;

/* Writes bytes into the FIFO queue, starts decoder task if necessary. */
int audio_stream_consumer(char *recv_buf, ssize_t bytes_read)
{
    // don't bother consuming bytes if stopped
    if(player_instance->command == CMD_STOP) {
		clientSilentDisconnect();
        return -2;
    }
	if (bytes_read >0) 
		spiRamFifoWrite(recv_buf, bytes_read);
	if (player_instance->decoder_status != RUNNING ) 
	{
//		t = 0;
		int bytes_in_buf = spiRamFifoFill();
		uint8_t fill_level = (bytes_in_buf * 100) / spiRamFifoLen();

		//bool buffer_ok = (fill_level > (bigSram()?15:80)); // in %
		if ((fill_level > (bigSram()?15:80)))
		{
			t = 0;
		// buffer is filled, start decoder
			if (start_decoder_task(player_instance) != 0) {
				ESP_LOGE(TAG, "Decoder task failed");
				audio_player_stop();
				clientDisconnect("decoder failed"); 
				return -1;
			}
		}
	}

	if (t == 0) {
		int bytes_in_buf = spiRamFifoFill();
		uint8_t fill_level = (bytes_in_buf * 100) / spiRamFifoLen();
		
		ESP_LOGI(TAG, "Buffer fill %u%%, %d // %d bytes", fill_level, bytes_in_buf,spiRamFifoLen());
	}
	t = (t+1) & 255;
	
    return 0;
}

void audio_player_init(player_t *player)
{
    player_instance = player;
    player_status = INITIALIZED;
}

void audio_player_start()
{
		player_instance->media_stream->eof = false;
		player_instance->command = CMD_START;
		player_instance->decoder_command = CMD_NONE;	
		player_status = RUNNING;
}

void audio_player_stop()
{ 
//		spiRamFifoReset();
		player_instance->decoder_command = CMD_STOP;
		player_instance->command = CMD_STOP;
		player_instance->media_stream->eof = true;
		player_instance->command = CMD_NONE;
		player_status = STOPPED;
}

component_status_t get_player_status()
{
    return player_status;
}
