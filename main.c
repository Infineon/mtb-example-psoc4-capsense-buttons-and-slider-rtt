/******************************************************************************
 * File Name: main.c
 *
 * Description: This is the source code for the PSoC 4 CAPSENSE Buttons
 * and Slider RTT code example for ModusToolbox.
 *
 * Related Document: See README.md
 *
 *******************************************************************************
 * Copyright 2023, Cypress Semiconductor Corporation (an Infineon company) or
 * an affiliate of Cypress Semiconductor Corporation.  All rights reserved.
 *
 * This software, including source code, documentation and related
 * materials ("Software") is owned by Cypress Semiconductor Corporation
 * or one of its affiliates ("Cypress") and is protected by and subject to
 * worldwide patent protection (United States and foreign),
 * United States copyright laws and international treaty provisions.
 * Therefore, you may use this Software only as provided in the license
 * agreement accompanying the software package from which you
 * obtained this Software ("EULA").
 * If no EULA applies, Cypress hereby grants you a personal, non-exclusive,
 * non-transferable license to copy, modify, and compile the Software
 * source code solely for use in connection with Cypress's
 * integrated circuit products.  Any reproduction, modification, translation,
 * compilation, or representation of this Software except as specified
 * above is prohibited without the express written permission of Cypress.
 *
 * Disclaimer: THIS SOFTWARE IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, NONINFRINGEMENT, IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. Cypress
 * reserves the right to make changes to the Software without notice. Cypress
 * does not assume any liability arising out of the application or use of the
 * Software or any product or circuit described in the Software. Cypress does
 * not authorize its products for use in any products where a malfunction or
 * failure of the Cypress product may reasonably be expected to result in
 * significant property damage, injury or death ("High Risk Product"). By
 * including Cypress's product in a High Risk Product, the manufacturer
 * of such system or application assumes all risk of such use and in doing
 * so agrees to indemnify Cypress against all liability.
 *******************************************************************************/

/*******************************************************************************
 * Include header files
 ******************************************************************************/
#include "cy_pdl.h"
#include "cybsp.h"
#include "cycfg.h"
#include "cycfg_capsense.h"

/*******************************************************************************
 * User configurable Macros/functions for RTT
 ********************************************************************************/
#include "SEGGER_RTT/RTT/SEGGER_RTT.h"
#include <stdio.h>

#define RTT_TUNER_CHANNEL 1
#define RTT_USE_FAST_RTT  1

#define RTT_TX_HEADER0      0x0Du
#define RTT_TX_HEADER1      0x0Au

#define RTT_TX_TAIL0        0x00u
#define RTT_TX_TAIL1        0xFFu
#define RTT_TX_TAIL2        0xFFu

typedef struct {
    uint8_t header[2];
    uint8_t tuner_data[sizeof(cy_capsense_tuner)];
    uint8_t tail[3];
} rtt_tuner_data_t;

static void rtt_tuner_send(void * context);
static void rtt_tuner_receive(uint8_t ** packet, uint8_t ** tuner_packet, void * context);
static uint8_t tuner_down_buf[32];

static rtt_tuner_data_t tuner_up_buf = {
    .header = {RTT_TX_HEADER0, RTT_TX_HEADER1},
    .tuner_data = {0},
    .tail = {RTT_TX_TAIL0, RTT_TX_TAIL1, RTT_TX_TAIL2}
};


/*******************************************************************************
 * Macros
 *******************************************************************************/
#define CAPSENSE_INTR_PRIORITY           (3u)
#define CY_ASSERT_FAILED                 (0u)

/*******************************************************************************
 * Function Prototypes
 *******************************************************************************/
static void initialize_capsense(void);
static void capsense_isr(void);
static void initialize_capsense_tuner(void);

/*******************************************************************************
 * Function Name: main
 ********************************************************************************
 * Summary:
 *  System entrance point. This function performs
 *  - initial setup of device
 *  - initialize CAPSENSE
 *  - initialize tuner communication
 *  - scan touch input continuously
 *
 * Return:
 *  int
 *
 *******************************************************************************/
int main(void)
{
    cy_rslt_t result = CY_RSLT_SUCCESS;

    /* Initializes the RTT Control Block */
    SEGGER_RTT_Init();
    /* Configure or add an up buffer by specifying its name, size and flags */
    SEGGER_RTT_ConfigUpBuffer(RTT_TUNER_CHANNEL, "tuner", &tuner_up_buf, sizeof(tuner_up_buf) + 1, SEGGER_RTT_MODE_NO_BLOCK_TRIM);
    /* Configure or add a down buffer by specifying its name, size and flags */
    SEGGER_RTT_ConfigDownBuffer(RTT_TUNER_CHANNEL, "tuner", tuner_down_buf, sizeof(tuner_down_buf), SEGGER_RTT_MODE_BLOCK_IF_FIFO_FULL);

    /* Initialize the device and board peripherals */
    result = cybsp_init();

    /* Board init failed. Stop program execution */
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(CY_ASSERT_FAILED);
    }

    /* Enable global interrupts */
    __enable_irq();

    /* Initialize CAPSENSE */
    initialize_capsense();

    /* Initialize CAPSENSE Tuner */
    initialize_capsense_tuner();

    /* Start the first scan */
    Cy_CapSense_ScanAllWidgets(&cy_capsense_context);

    for (;;)
    {
        if(CY_CAPSENSE_NOT_BUSY == Cy_CapSense_IsBusy(&cy_capsense_context))
        {
            /* Process all widgets */
            Cy_CapSense_ProcessAllWidgets(&cy_capsense_context);

            /* Establishes synchronized communication with the CAPSENSE Tuner tool */
            Cy_CapSense_RunTuner(&cy_capsense_context);

            /* Start the next scan */
            Cy_CapSense_ScanAllWidgets(&cy_capsense_context);

        }
    }
}


/*******************************************************************************
 * Function Name: initialize_capsense
 ********************************************************************************
 * Summary:
 *  This function initializes the CAPSENSE and configures the CAPSENSE
 *  interrupt.
 *
 *******************************************************************************/
static void initialize_capsense(void)
{
    cy_capsense_status_t status = CY_CAPSENSE_STATUS_SUCCESS;

    /* CAPSENSE interrupt configuration */
    static const cy_stc_sysint_t capsense_interrupt_config =
    {
        .intrSrc = CYBSP_CSD_IRQ,
        .intrPriority = CAPSENSE_INTR_PRIORITY,
    };

    /* Capture the CSD HW block and initialize it to the default state. */
    status = Cy_CapSense_Init(&cy_capsense_context);

    if (CY_CAPSENSE_STATUS_SUCCESS == status)
    {
        /* Initialize CAPSENSE interrupt */
        Cy_SysInt_Init(&capsense_interrupt_config, capsense_isr);
        NVIC_ClearPendingIRQ(capsense_interrupt_config.intrSrc);
        NVIC_EnableIRQ(capsense_interrupt_config.intrSrc);

        /* Initialize the CAPSENSE firmware modules. */
        status = Cy_CapSense_Enable(&cy_capsense_context);
    }

    if(status != CY_CAPSENSE_STATUS_SUCCESS)
    {
        /* This status could fail before tuning the sensors correctly.
         * Ensure that this function passes after the CapSense sensors are tuned
         * as per procedure given in the README.md file */
    }
}


/*******************************************************************************
 * Function Name: capsense_isr
 ********************************************************************************
 * Summary:
 *  Wrapper function for handling interrupts from CAPSENSE block.
 *
 *******************************************************************************/
static void capsense_isr(void)
{
    Cy_CapSense_InterruptHandler(CYBSP_CSD_HW, &cy_capsense_context);
}


/*******************************************************************************
 * Function Name: initialize_capsense_tuner
 ********************************************************************************
 * Summary:
 *  Initializes interface between Tuner GUI and PSoC 4 MCU.
 *
 *******************************************************************************/
static void initialize_capsense_tuner(void)
{
    cy_capsense_context.ptrInternalContext->ptrTunerSendCallback    = rtt_tuner_send;
    cy_capsense_context.ptrInternalContext->ptrTunerReceiveCallback = rtt_tuner_receive;
}


/*******************************************************************************
 * Function Name: rtt_tuner_send
 ********************************************************************************
 * Summary:
 *  This function sends the CAPSENSE data to Tuner through RTT
 *
 *******************************************************************************/
static void rtt_tuner_send(void * context)
{
    (void)context;

    SEGGER_RTT_LOCK();
    SEGGER_RTT_BUFFER_UP *buffer = _SEGGER_RTT.aUp + RTT_TUNER_CHANNEL;
    buffer->RdOff = 0u;
    buffer->WrOff = sizeof(tuner_up_buf);

    memcpy(tuner_up_buf.tuner_data, &cy_capsense_tuner, sizeof(cy_capsense_tuner));
    SEGGER_RTT_UNLOCK();
}


/*******************************************************************************
 * Function Name: rtt_tuner_receive
 ********************************************************************************
 * Summary:
 *  RTT receives the Tuner command
 *
 *******************************************************************************/
static void rtt_tuner_receive(uint8_t ** packet, uint8_t ** tuner_packet, void * context)
{
    uint32_t i;
    static uint32_t data_index = 0u;
    static uint8_t command_packet[16u] = {0u};
    while(0 != SEGGER_RTT_HasData(RTT_TUNER_CHANNEL))
    {
        uint8_t byte;
        SEGGER_RTT_Read(RTT_TUNER_CHANNEL, &byte, 1);
        command_packet[data_index++] = byte;
        if (CY_CAPSENSE_COMMAND_PACKET_SIZE == data_index)
        {
            if (CY_CAPSENSE_COMMAND_OK == Cy_CapSense_CheckTunerCmdIntegrity(&command_packet[0u]))
            {
                data_index = 0u;
                *tuner_packet = (uint8_t *)&cy_capsense_tuner;
                *packet = &command_packet[0u];
                break;
            }
            else
            {
                data_index--;
                for(i = 0u; i < (CY_CAPSENSE_COMMAND_PACKET_SIZE - 1u); i++)
                {
                    command_packet[i] = command_packet[i + 1u];
                }
            }
        }
    }
}


/* [] END OF FILE */
