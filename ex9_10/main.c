/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body  -  Lab 2, Exercises 9 and 10
  *                   built on the Exercise 8 architecture.
  *
  *                   The seven-segment side is the digital clock from Ex5/Ex7,
  *                   paced by software timers. The 8x8 LED matrix shows the
  *                   character 'A' and scrolls it to the left.
  *
  *                   The TIM2 interrupt does nothing but count the software
  *                   timers down; every piece of application logic and every
  *                   GPIO write happens in main().
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under BSD 3-Clause license,
  * the "License"; You may not use this file except in compliance with the
  * License. You may obtain a copy of the License at:
  *                        opensource.org/licenses/BSD-3-Clause
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* Columns are the LED anodes, fed from +3.3V through the R1 pack. When ENMc is
 * HIGH the ULN2803 shorts column c to GND and it goes dark, so a column is
 * selected by driving ENMc LOW. Rows are the cathodes: a pixel lights when its
 * ROW is LOW. Both confirmed with a one-pixel test in Proteus. */
#define MATRIX_ENM_ACTIVE   GPIO_PIN_RESET
#define MATRIX_ENM_IDLE     GPIO_PIN_SET

#define MATRIX_DATA_ON      GPIO_PIN_RESET
#define MATRIX_DATA_OFF     GPIO_PIN_SET

/* Exercise 10 timing: hold the 'A' still after reset, then one column per step */
#define MATRIX_HOLD_MS      3000
#define MATRIX_SHIFT_MS     500
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
TIM_HandleTypeDef htim2;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_TIM2_Init(void);
/* USER CODE BEGIN PFP */
void display7SEG(int num);
void turnOffAll7SEG(void);
void update7SEG(int index);
void updateClockBuffer(void);
void updateLEDMatrix(int index);
void shiftMatrixLeft(void);
void setTimer0(int duration);
void setTimer1(int duration);
void setTimer2(int duration);
void setTimer3(int duration);
void timer_run(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* ==================== Software timers (Exercise 6) ==================== */

/* The timer interrupt period in milliseconds. setTimer_() turns a duration
 * in ms into a number of interrupts by dividing by this. */
int TIMER_CYCLE = 10;

/* timer0 paces the clock, timer1 the DOT, timer2 the seven-segment scan,
 * timer3 the matrix scroll. All are written by the interrupt and read by
 * main(), so they are volatile: without it an optimising build may hold a
 * flag in a register and never see the interrupt change it. */
volatile int timer0_counter = 0;
volatile int timer0_flag    = 0;

volatile int timer1_counter = 0;
volatile int timer1_flag    = 0;

volatile int timer2_counter = 0;
volatile int timer2_flag    = 0;

volatile int timer3_counter = 0;
volatile int timer3_flag    = 0;

void setTimer0(int duration) {
    timer0_counter = duration / TIMER_CYCLE;
    timer0_flag    = 0;
}

void setTimer1(int duration) {
    timer1_counter = duration / TIMER_CYCLE;
    timer1_flag    = 0;
}

void setTimer2(int duration) {
    timer2_counter = duration / TIMER_CYCLE;
    timer2_flag    = 0;
}

void setTimer3(int duration) {
    timer3_counter = duration / TIMER_CYCLE;
    timer3_flag    = 0;
}

/**
  * @brief  Count every software timer down one tick. This is the only work
  *         the interrupt does. A timer that reaches zero raises its flag and
  *         then stops, until setTimer_() arms it again.
  */
void timer_run(void) {
    if (timer0_counter > 0) {
        timer0_counter--;
        if (timer0_counter == 0) timer0_flag = 1;
    }
    if (timer1_counter > 0) {
        timer1_counter--;
        if (timer1_counter == 0) timer1_flag = 1;
    }
    if (timer2_counter > 0) {
        timer2_counter--;
        if (timer2_counter == 0) timer2_flag = 1;
    }
    if (timer3_counter > 0) {
        timer3_counter--;
        if (timer3_counter == 0) timer3_flag = 1;
    }
}

/* ==================== Seven-segment decoding (Lab 1) ==================== */

/* segOn[digit][segment], 1 = segment lit.
 * SEG0 = a, SEG1 = b, SEG2 = c, SEG3 = d, SEG4 = e, SEG5 = f, SEG6 = g */
const uint8_t segOn[10][7] = {
    {1,1,1,1,1,1,0}, // 0
    {0,1,1,0,0,0,0}, // 1
    {1,1,0,1,1,0,1}, // 2
    {1,1,1,1,0,0,1}, // 3
    {0,1,1,0,0,1,1}, // 4
    {1,0,1,1,0,1,1}, // 5
    {1,0,1,1,1,1,1}, // 6
    {1,1,1,0,0,0,0}, // 7
    {1,1,1,1,1,1,1}, // 8
    {1,1,1,1,0,1,1}  // 9
};

GPIO_TypeDef* segPorts[7] = { SEG0_GPIO_Port, SEG1_GPIO_Port, SEG2_GPIO_Port, SEG3_GPIO_Port,
                              SEG4_GPIO_Port, SEG5_GPIO_Port, SEG6_GPIO_Port };
uint16_t      segPins[7]  = { SEG0_Pin, SEG1_Pin, SEG2_Pin, SEG3_Pin,
                              SEG4_Pin, SEG5_Pin, SEG6_Pin };

/* Common anode: a segment lights when its pin is driven LOW */
void display7SEG(int num) {
    if (num < 0 || num > 9) return;
    for (int i = 0; i < 7; i++) {
        HAL_GPIO_WritePin(segPorts[i], segPins[i],
                          segOn[num][i] ? GPIO_PIN_RESET : GPIO_PIN_SET);
    }
}

/* ==================== Scanning the displays (Exercise 3) ==================== */

const int MAX_LED   = 4;
int       index_led = 0;
int       led_buffer[4] = {0, 0, 0, 0};   /* filled by updateClockBuffer() */

void turnOffAll7SEG(void) {
    HAL_GPIO_WritePin(EN0_GPIO_Port, EN0_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(EN1_GPIO_Port, EN1_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(EN2_GPIO_Port, EN2_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(EN3_GPIO_Port, EN3_Pin, GPIO_PIN_SET);
}

/**
  * @brief  Light one of the four displays with its buffer value.
  * @param  index which display to light, 0..3
  */
void update7SEG(int index) {
    turnOffAll7SEG();
    switch (index) {
        case 0:
            display7SEG(led_buffer[0]);
            HAL_GPIO_WritePin(EN0_GPIO_Port, EN0_Pin, GPIO_PIN_RESET);
            break;
        case 1:
            display7SEG(led_buffer[1]);
            HAL_GPIO_WritePin(EN1_GPIO_Port, EN1_Pin, GPIO_PIN_RESET);
            break;
        case 2:
            display7SEG(led_buffer[2]);
            HAL_GPIO_WritePin(EN2_GPIO_Port, EN2_Pin, GPIO_PIN_RESET);
            break;
        case 3:
            display7SEG(led_buffer[3]);
            HAL_GPIO_WritePin(EN3_GPIO_Port, EN3_Pin, GPIO_PIN_RESET);
            break;
        default:
            break;
    }
}

/* ==================== Digital clock (Exercise 5) ==================== */

int hour = 15, minute = 8, second = 50;

/**
  * @brief  Split hour and minute into the four digits of led_buffer.
  *         Integer division supplies the leading zero by itself: minute 8
  *         gives 8/10 = 0 and 8%10 = 8, so the display reads "08".
  */
void updateClockBuffer(void) {
    led_buffer[0] = hour / 10;
    led_buffer[1] = hour % 10;
    led_buffer[2] = minute / 10;
    led_buffer[3] = minute % 10;
}

/* ==================== LED matrix (Exercise 9) ==================== */

/* Column-select pins, driven through the ULN2803. ENM selects a COLUMN,
 * not a row -- confirmed against the schematic. */
GPIO_TypeDef* enmPorts[8] = { ENM0_GPIO_Port, ENM1_GPIO_Port, ENM2_GPIO_Port, ENM3_GPIO_Port,
                              ENM4_GPIO_Port, ENM5_GPIO_Port, ENM6_GPIO_Port, ENM7_GPIO_Port };
uint16_t      enmPins[8]  = { ENM0_Pin, ENM1_Pin, ENM2_Pin, ENM3_Pin,
                              ENM4_Pin, ENM5_Pin, ENM6_Pin, ENM7_Pin };

/* Row-data pins ROW0..ROW7 = PB8..PB15, they carry the 8 row bits of
 * whichever column is currently selected */
GPIO_TypeDef* rowPorts[8] = { ROW0_GPIO_Port, ROW1_GPIO_Port, ROW2_GPIO_Port, ROW3_GPIO_Port,
                              ROW4_GPIO_Port, ROW5_GPIO_Port, ROW6_GPIO_Port, ROW7_GPIO_Port };
uint16_t      rowPins[8]  = { ROW0_Pin, ROW1_Pin, ROW2_Pin, ROW3_Pin,
                              ROW4_Pin, ROW5_Pin, ROW6_Pin, ROW7_Pin };

/* matrix_buffer[c] = the 8 row bits shown while column c (ENMc) is enabled.
 * Bit 0 -> ROW0 (top row), bit 7 -> ROW7 (bottom row).
 *
 * Character 'A':
 *   . . # # # # . .
 *   . # . . . . # .
 *   . # . . . . # .
 *   . # . . . . # .
 *   . # # # # # # .
 *   . # . . . . # .
 *   . # . . . . # .
 *   . # . . . . # .
 */
uint8_t matrix_buffer[8] = { 0x00, 0xFE, 0x11, 0x11, 0x11, 0x11, 0xFE, 0x00 };

int matrix_index = 0;   /* column currently enabled, tracked by updateLEDMatrix */
int matrix_scan  = 0;   /* column main() will draw on its next pass */

/**
  * @brief  Put one column of matrix_buffer on the display.
  *         Only one column is enabled at a time; calling this repeatedly for
  *         index 0..7 fast enough makes the whole 8x8 picture appear.
  * @param  index column to show, 0..7
  */
void updateLEDMatrix(int index) {
    if (index < 0 || index > 7) return;

    /* 1. switch the previously enabled column off, so the new data cannot
     *    briefly appear on the wrong column (ghosting) */
    HAL_GPIO_WritePin(enmPorts[matrix_index], enmPins[matrix_index], MATRIX_ENM_IDLE);

    /* 2. put the 8 row bits of this column on ROW0..ROW7 */
    for (int bit = 0; bit < 8; bit++) {
        HAL_GPIO_WritePin(rowPorts[bit], rowPins[bit],
                          ((matrix_buffer[index] >> bit) & 0x01) ? MATRIX_DATA_ON
                                                                 : MATRIX_DATA_OFF);
    }

    /* 3. enable this column */
    HAL_GPIO_WritePin(enmPorts[index], enmPins[index], MATRIX_ENM_ACTIVE);

    matrix_index = index;
}

/* ==================== Matrix animation (Exercise 10) ==================== */

/**
  * @brief  Slide the picture one column to the left. Each column takes the
  *         value of the one to its right; column 0 wraps round to column 7.
  *         main() only calls this between complete frames, so a shift can
  *         never land halfway through a scan and tear the image.
  */
void shiftMatrixLeft(void) {
    uint8_t first = matrix_buffer[0];
    for (int c = 0; c < 7; c++) {
        matrix_buffer[c] = matrix_buffer[c + 1];
    }
    matrix_buffer[7] = first;
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_TIM2_Init();
  /* USER CODE BEGIN 2 */
  /* every seven-segment off, DOT off, every matrix column off */
  turnOffAll7SEG();
  HAL_GPIO_WritePin(DOT_GPIO_Port, DOT_Pin, GPIO_PIN_SET);
  for (int i = 0; i < 8; i++) {
      HAL_GPIO_WritePin(enmPorts[i], enmPins[i], MATRIX_ENM_IDLE);
  }

  /* show the starting time straight away instead of 00:00 */
  updateClockBuffer();

  HAL_TIM_Base_Start_IT(&htim2);

  /* every software timer must be armed before the loop, or its flag never
   * rises and its branch below never runs */
  setTimer0(1000);              /* one second per clock tick          */
  setTimer1(1000);              /* one second per DOT toggle          */
  setTimer2(10);                /* 10ms per seven-segment display     */
  setTimer3(MATRIX_HOLD_MS);    /* hold the 'A' still, then scroll    */
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
      /* ---- the clock, once per second ---- */
      if (timer0_flag == 1) {
          setTimer0(1000);          /* re-arm; this also clears timer0_flag */

          second++;
          if (second >= 60) {
              second = 0;
              minute++;
          }
          if (minute >= 60) {
              minute = 0;
              hour++;
          }
          if (hour >= 24) {
              hour = 0;
          }

          updateClockBuffer();
      }

      /* ---- the DOT LEDs, once per second ---- */
      if (timer1_flag == 1) {
          setTimer1(1000);          /* re-arm; this also clears timer1_flag */
          HAL_GPIO_TogglePin(DOT_GPIO_Port, DOT_Pin);
      }

      /* ---- the seven-segment scan, one display every 10ms ----
       * 4 displays x 10ms = 40ms per pass (25Hz), so the clock reads as
       * four steady digits. */
      if (timer2_flag == 1) {
          setTimer2(10);            /* re-arm; this also clears timer2_flag */

          update7SEG(index_led);

          index_led++;
          if (index_led >= MAX_LED) {
              index_led = 0;
          }
      }

      /* ---- the matrix scan, one column per pass of this loop ----
       * The software timers cannot help here: their resolution is one tick
       * (10ms), and 8 columns x 10ms would be a 12.5Hz frame that flickers
       * badly. So each column is held for 4ms instead, the same timing that
       * was verified in Proteus: 8 x 4ms = 32ms per frame (~31Hz).
       * Only ONE column is drawn per pass, so the loop comes round every 4ms
       * and the timer branches above are still serviced promptly. */
      updateLEDMatrix(matrix_scan);
      HAL_Delay(4);

      matrix_scan++;
      if (matrix_scan >= 8) {
          matrix_scan = 0;

          /* Exercise 10: a whole frame has just finished, which is the only
           * safe moment to change matrix_buffer. */
          if (timer3_flag == 1) {
              setTimer3(MATRIX_SHIFT_MS);
              shiftMatrixLeft();
          }
      }

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief TIM2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM2_Init(void)
{

  /* USER CODE BEGIN TIM2_Init 0 */

  /* USER CODE END TIM2_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM2_Init 1 */

  /* USER CODE END TIM2_Init 1 */
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 7999;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 9;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim2, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM2_Init 2 */

  /* USER CODE END TIM2_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, DOT_Pin|Led_Red_Pin|EN0_Pin|EN1_Pin
                          |EN2_Pin|EN3_Pin|ENM0_Pin|ENM1_Pin
                          |ENM2_Pin|ENM3_Pin|ENM4_Pin|ENM5_Pin
                          |ENM6_Pin|ENM7_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, SEG0_Pin|SEG1_Pin|SEG2_Pin|SEG3_Pin
                          |SEG4_Pin|SEG5_Pin|SEG6_Pin|ROW0_Pin
                          |ROW1_Pin|ROW2_Pin|ROW3_Pin|ROW4_Pin
                          |ROW5_Pin|ROW6_Pin|ROW7_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : DOT_Pin Led_Red_Pin EN0_Pin EN1_Pin
                           EN2_Pin EN3_Pin ENM0_Pin ENM1_Pin
                           ENM2_Pin ENM3_Pin ENM4_Pin ENM5_Pin
                           ENM6_Pin ENM7_Pin */
  GPIO_InitStruct.Pin = DOT_Pin|Led_Red_Pin|EN0_Pin|EN1_Pin
                          |EN2_Pin|EN3_Pin|ENM0_Pin|ENM1_Pin
                          |ENM2_Pin|ENM3_Pin|ENM4_Pin|ENM5_Pin
                          |ENM6_Pin|ENM7_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : SEG0_Pin SEG1_Pin SEG2_Pin SEG3_Pin
                           SEG4_Pin SEG5_Pin SEG6_Pin ROW0_Pin
                           ROW1_Pin ROW2_Pin ROW3_Pin ROW4_Pin
                           ROW5_Pin ROW6_Pin ROW7_Pin */
  GPIO_InitStruct.Pin = SEG0_Pin|SEG1_Pin|SEG2_Pin|SEG3_Pin
                          |SEG4_Pin|SEG5_Pin|SEG6_Pin|ROW0_Pin
                          |ROW1_Pin|ROW2_Pin|ROW3_Pin|ROW4_Pin
                          |ROW5_Pin|ROW6_Pin|ROW7_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

}

/* USER CODE BEGIN 4 */

/**
  * @brief  Period elapsed callback in non blocking mode, invoked every 10ms by TIM2.
  *
  *         Exercise 8: this is the whole interrupt. It holds no application
  *         logic and touches no GPIO, so its execution time is short and
  *         constant no matter what the displays are doing.
  *
  * @param  htim TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM2) {
      timer_run();
  }
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
