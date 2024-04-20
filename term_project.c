#include "LPC17xx.h"
#include "lpc17xx_timer.h"
#include "lpc17xx_libcfg_default.h"
#include "lpc17xx_pinsel.h"
#include "lpc17xx_uart.h"
#include "debug_frmwrk.h" 
#include "Board_LED.h"

uint32_t LEDOn, LEDOff;

// =================================================================================================================================================================================

//  MODE 관련 매크로 선언

#define MENU_MODE 0
#define CAR_SELECT_MODE 1 
#define TIM_SELECT_MODE 2
#define SET_PASSWORD_MODE 3 
#define REALTIME_MODE 4
#define AGENCY_MODE 5 
#define TRANSFER_USERINFO_MODE 6 
#define REQUEST_AGENCYINFO_MODE 7

// 차량별 시간,비용 매크로 선언
#define S3 20
#define S6 30
#define S9 40

// ================================================================================================================================================================================


// 유저정보 구조체 
typedef struct {
		char password[6];
    char userid;
    char carname;
    char cost;
} USER;

// 타이머 별 실시간 제작현황 flag 구조체 
typedef struct{
	char empty;
	char percent0;
	char percent25;
	char percent50;
	char percent75;
	char percent100;
} REALTIME_FLAG;


REALTIME_FLAG tim0_realtime_flag, tim1_realtime_flag, tim2_realtime_flag, tim3_realtime_flag; 

USER user; 

USER tim0_fifo_user[20];
USER tim1_fifo_user[20];
USER tim2_fifo_user[20];
USER tim3_fifo_user[20]; // 타이머(공장)별 FIFO구조의 USER타입의 구조체 배열 

char tim0_front =-1, tim1_front = -1, tim2_front = -1, tim3_front = -1;  // 구조체배열에서 가장 최근에 들어온 원소를 가리키는 인덱스


int i = 0;																																										 	// UART 0 , UI 출력을 위한 인덱스
int transfer_idx= 0; 																																			// UART 1 , 데이터 전송을 위한 인덱스
int user_idx = 0, password_idx = 0;																						//유저번호 및 비밀번호를 위한 인덱스 

char garage_num = '0' , making_tim_num = '0';												 // 현재 상대방(대리점) 차고지 개수와 공장에서 재작중인 차량개수

// 			uart를 통한 입,출력 인터럽트 발생 flag 
char uart0_tx_menu = 0, uart0_tx_agency_info = 0, uart0_tx_car_select = 0, uart0_tx_tim_select = 0, uart0_tx_set_password =0,  uart0_rx_eflg = 0, uart0_tx_realtime = 0;
char uart1_tx_userinfo = 0, uart1_tx_request_agency_info = 0; 					
char uart0_rx_data, uart1_rx_data; 																						// rx를 통해 받은 데이터 

// 			UI출력을 위한 문자열
const char menu[] = "\r\n서울과학자동차입니다 무엇을 도와드릴까요?\r\n1. 구매  2. 실시간 차량 제작 확인 abc\r\n";
const char agency_info[] = "\r\n현재위치에서 이용가능한 차량대리점 공릉대리점입니다. (연결중..) \r\n";
const char car_select[] = "\r현재 공릉대리점 차고지 차량개수: a대 / 4대 \r\n해당 대리점 대상 차량 주문개수: b대 / 4대\r\n\r\n구매하고자 하는 차량을 선택하세요( SPACE: 뒤로 이동 )\r\n1. Seoultech3 (비용 : 2천만원 / 제작기간 : 20초) \r\n2. Seoultech6 (비용 : 3천만원 / 제작기간 : 30초) \r\n3. Seoultech9 (비용 : 4천만원 / 제작기간 : 40초)\r\n";
const char tim_select[] = "\r\n제작을 맡길 공장을 선택하세요( SPACE: 뒤로 이동 )\r\n1. 공장0 ( 기본 ) \r\n2. 공장1 ( 제작속도 2배 / 비용 25% 증가 )\r\n3. 공장2 ( 제작속도 4배 / 비용 50% 증가 )\r\n4. 공장3 ( 제작속도 8배 / 비용 100% 증가 ) \r\n";
const char set_password[] = "\r\n차량을 받을때 인증할 비밀번호 설정 ( SPACE: 뒤로 이동 , BACKSPACE : 다시 설정)\r\n"; 
const char tim_full[] = "\r\n선택한 공장은 현재 가동중이여서 제작할 수 없습니다.\7\r\n";
const char realtime[] = "\r\n공장a : 고객번호 b / 차종 Sc / 비용 de00만원 / 제작률 ghij \r\n";
const char garage_full[] = "\r\n해당 대리점이 포화상태입니다. 나중에 다시 이용해주세요.\7\r\n" ;																																															

// 				모드 제어 선언 
char menu_mode = 0 , agency_mode = 0, car_select_mode = 0, set_password_mode =0 ,tim_select_mode =0, realtime_mode = 0;             												// uart0 관련 
char transfer_userinfo_mode = 0, request_agencyinfo_mode =0; 																																																		               												// uart1 관련 

//				나머지 flag
char is_realtime = 1; 																																		// 실시간 제작현황 끄기 켜기 flag 
char is_setting = 0; 																																		// 비밀번호 세팅중임을 나타내는 flag
char tim_is_full = 0; 																																		// 공장이 가동중임을 나타내는 flag
char order_is_full = 0;																																// 대리점이 꽉참을 나타내는 flag
char purchase_is_full = 0; 																												// 구매요청이 포화임을 나타내는 flag
char realtime_flag_tim[4] = {0, 0, 0, 0};                                      //공장별 실시간 제작현황 출력을 위한 flag

//TIM, UART 함수로 제어하기 위한 구조체 변수 
UART_CFG_Type UART_config_struct;
UART_FIFO_CFG_Type UART_FIFO_config_struct;
TIM_TIMERCFG_Type TIM1_ConfigStruct,TIM3_ConfigStruct ;
TIM_MATCHCFG_Type TIM1_MatchConfigStruct, TIM3_MatchConfigStruct; 


// ================================================================================================================================================================================


//				함수선언
void UART0_Init_ByFunc();
void UART1_Init_ByReg();
void TIMER0_Init_ByReg();
void TIMER0_MakeCar_ByReg(char CAR_NAME);
void TIMER1_Init_ByFunc();
void TIMER1_MakeCar_ByFunc(char CAR_NAME);
void TIMER2_Init_ByReg();
void TIMER2_MakeCar_ByReg(char CAR_NAME);
void TIMER3_Init_ByFunc();
void TIMER3_MakeCar_ByFunc(char CAR_NAME);
void setInterruptPriorities();

void uart0_set_mode(char mode_name);
void uart1_set_mode(char mode_name);


// ================================================================================================================================================================================

// Main 함수 

	int main(void){
			LED_On(0);
			SystemCoreClockUpdate();
			LED_Initialize();
			setInterruptPriorities();
			UART0_Init_ByFunc();
			UART1_Init_ByReg();
			TIMER0_Init_ByReg();
			TIMER1_Init_ByFunc();
			TIMER2_Init_ByReg();
			TIMER3_Init_ByFunc(); 																					// UART 및 TIMER(공장) 초기화
		
			tim0_realtime_flag.empty = tim1_realtime_flag.empty = tim2_realtime_flag.empty = tim3_realtime_flag.empty = 1; // 공장 미가동중
			uart0_tx_menu = 1;
			while(1)
			{
						if(uart0_tx_menu) // TX : 메뉴 UI 출력
						{
								uart0_set_mode(MENU_MODE);
								LPC_UART0->THR = '\0';
								
								uart0_tx_menu = 0;
						}
						else if(uart0_tx_agency_info) // TX : 구매 1. 대리점 현황 확인
						{
							uart0_set_mode(AGENCY_MODE);
							UART_SendByte( LPC_UART0, '\0');
								
							uart0_tx_agency_info = 0;
						}
						else if(uart0_tx_car_select) // TX :  구매 2. 차 선택 UI 출력 
						{
								uart0_set_mode(CAR_SELECT_MODE);
								LPC_UART0->THR = '\0';
								
								uart0_tx_car_select = 0;
						}
						else if(uart0_tx_set_password) // TX : 구매 3. 고객 비밀번호 설정 UI 출력 
						{
								uart0_set_mode(SET_PASSWORD_MODE);
								UART_SendByte( LPC_UART0, '\0');
								
								uart0_tx_set_password= 0;
						}
						else if(uart0_tx_tim_select) // TX : 구매 4. 공장 선택 UI 출력 
						{
								uart0_set_mode(TIM_SELECT_MODE);
								LPC_UART0->THR = '\0';
								
								uart0_tx_tim_select = 0;
						}
						else if(uart0_tx_realtime) // TX : 실시간 제작현황  UI 출력
						{
								uart0_set_mode(REALTIME_MODE);
								UART_SendByte( LPC_UART0, '\0');
							
								uart0_tx_realtime = 0;
						}
						else if(uart0_rx_eflg) 			// RX 
						{
							if(menu_mode) // RX : 메뉴 UI에서 사용자입력 처리 
								{
											if(uart0_rx_data == '1') uart0_tx_agency_info =1;													// 메뉴선택
											else if(uart0_rx_data == '2')
												{
													if(is_realtime) is_realtime = 0;
													else is_realtime = 1;
													uart0_tx_menu =1;
											}																																																			
								}
								else if(car_select_mode) //  RX : 구매 2. 차 선택 UI에서 사용자입력 처리 
								{
										if(uart0_rx_data >= '1' && uart0_rx_data <= '3')
										{
												if(uart0_rx_data == '1')		user.carname = '3';															// 차량선택
												else if(uart0_rx_data == '2')		user.carname = '6';
												else if(uart0_rx_data == '3')		user.carname = '9';											
												uart0_tx_set_password = 1;
										}
										else if(uart0_rx_data == ' ') // SPACE : 뒤로가기
										{ 
												uart0_tx_menu = 1;
										}
								}
								else if(set_password_mode) //  RX : 구매 3. 비밀번호 설정  UI에서 비밀번호 입력 처리
								{
									is_setting = 1; // 비번 세팅중 
									if(uart0_rx_data >= '0' && uart0_rx_data <= '9' )
									{
											user.password[password_idx++] = uart0_rx_data; 									// 비밀번호 저장
											LPC_UART0->THR = uart0_rx_data;
											if(password_idx >= 6)
											{
												LPC_UART0->THR = '\n';
												uart0_tx_tim_select = 1;
												is_setting = 0; // 비번 세팅 끝
												password_idx = 0;
											}
									}
									else if(uart0_rx_data == '\b') // BACK SPACE : 처음부터 입력 
									{
											UART_SendByte( LPC_UART0, '\r');;
											password_idx = 0;
									}
									else if(uart0_rx_data == ' ') 
									{ 
											LPC_UART0->THR = '\n';
											is_setting = 0;
											password_idx = 0;
											uart0_tx_car_select = 1;
									}
								}
								else if(tim_select_mode) //  RX : 구매 4. 공장 선택 UI에서 사용자입력 처리 
								{
									
										if (uart0_rx_data >= '1' && uart0_rx_data <= '4') // 공장 선택시 바로 제작하도록
										{
												if(user_idx == 10) user_idx = 0;
												user.userid = user_idx;

												if(uart0_rx_data == '1') // 공장 0 선택 
												{
													if(!tim0_realtime_flag.empty) { uart0_tx_tim_select = 1; tim_is_full = 1;}					// 공장0이 가동중이면 제작불가
													else if(user.carname == '3' || user.carname == '6' || user.carname == '9')
													{
																switch (user.carname) 
																{
																		case '3': { TIMER0_MakeCar_ByReg(S3); user.cost = S3; break;}
																		case '6':	{ TIMER0_MakeCar_ByReg(S6); user.cost = S6; break; }
																		case '9': { TIMER0_MakeCar_ByReg(S9); user.cost = S9; break;} 			// 선택시 TIMER(공장) 동작
																}
																tim0_fifo_user[++tim0_front] = user;
																making_tim_num++; user_idx++;
																uart0_tx_menu = 1;
													}
												}
												else if(uart0_rx_data == '2') // 공장 1 선택 
												{
													if(!tim1_realtime_flag.empty) { uart0_tx_tim_select = 1; tim_is_full = 1;}					// 공장1이 가동중이면 제작불가
													else if(user.carname == '3' || user.carname == '6' || user.carname == '9')
													{
																switch (user.carname) 
																{
																		case '3': { TIMER1_MakeCar_ByFunc(S3); user.cost = (char)(1.25*S3); break;}
																		case '6':	{ TIMER1_MakeCar_ByFunc(S6); user.cost = (char)(1.25*S6); break; }
																		case '9': { TIMER1_MakeCar_ByFunc(S9); user.cost = (char)(1.25*S9); break;}
																}
																tim1_fifo_user[++tim1_front] = user;
																making_tim_num++; user_idx++;
																uart0_tx_menu = 1;
													}
												}
												else if(uart0_rx_data == '3') // 공장 2 선택 
												{
													if(!tim2_realtime_flag.empty) { uart0_tx_tim_select = 1; tim_is_full = 1;}					// 공장2이 가동중이면 제작불가
													else if(user.carname == '3' || user.carname == '6' || user.carname == '9')
													{
																switch (user.carname) 
																{
																		case '3': { TIMER2_MakeCar_ByReg(S3); user.cost = (char)(1.5*S3); break;}
																		case '6':	{ TIMER2_MakeCar_ByReg(S6); user.cost = (char)(1.5*S6); break; }
																		case '9': { TIMER2_MakeCar_ByReg(S9); user.cost = (char)(1.5*S9); break;}
																}
																tim2_fifo_user[++tim2_front] = user;
																making_tim_num++; user_idx++;
																uart0_tx_menu = 1;
													}
												}
												else if(uart0_rx_data == '4') // 공장 3 선택 
												{
													if(!tim3_realtime_flag.empty) { uart0_tx_tim_select = 1; tim_is_full = 1;}					// 공장3이 가동중이면 제작불가
													else if(user.carname == '3' || user.carname == '6' || user.carname == '9')
													{
																switch (user.carname) 
																{
																		case '3': { TIMER3_MakeCar_ByFunc(S3); user.cost = (char)(2*S3); break;}
																		case '6':	{ TIMER3_MakeCar_ByFunc(S6); user.cost = (char)(2*S6); break; }
																		case '9': { TIMER3_MakeCar_ByFunc(S9); user.cost = (char)(2*S9); break;}
																}
																tim3_fifo_user[++tim3_front] = user;
																making_tim_num++; user_idx++;
																uart0_tx_menu = 1;
													}
												}
										}
										else if(uart0_rx_data == ' ')
										{ 
											uart0_tx_set_password = 1;
										}
								}
								uart0_rx_eflg =0; 
						}
						else if(uart1_tx_userinfo) // 상대보드로 TX : 차량 제작 완료시 대리점으로 유저정보전달 
						{
								LPC_UART1->THR = '\0';
								uart1_tx_userinfo = 0;
						}
						else if(uart1_tx_request_agency_info) // 상대보드로 TX : 대리점에 차고지 갯수 요청
						{
								LPC_UART1->THR = 'r';
								uart1_tx_request_agency_info = 0;
						}
			}
}


// ================================================================================================================================================================================

// IRQ Handler

void UART0_IRQHandler(void){  //  UART0 : PC, 하이퍼터미널과 연결 && 함수,레지스터를 번갈아사용 
	
		int32_t int_status;
		int32_t status;
	
		int_status = UART_GetIntId(LPC_UART0);
		
		if((int_status & 0x0000000F) == 2) // THRE interrupt check
		{
				if(menu_mode) 													// TX : 메뉴 UI 출력
				{
						if(menu[i] != '\0')
						{
							if(menu[i] == 'a')  UART_SendByte( LPC_UART0,  'O');
							else if(menu[i] == 'b')
							{
								if(is_realtime)  UART_SendByte( LPC_UART0, 'F');
								else  UART_SendByte( LPC_UART0, 'N');
							}
							else if(menu[i] == 'c')
							{
								if(is_realtime)  UART_SendByte( LPC_UART0, 'F');
								else  UART_SendByte( LPC_UART0, ' ');
							}
							else
								LPC_UART0->THR = menu[i];
							i++;
						}
						else
								i = 0;
				}
				else if(agency_mode)   						 // TX : 구매 1. 대리점 현황 확인
				{
						if(agency_info[i] != '\0')
								 LPC_UART0->THR = agency_info[i++];
						else
						{
								i = 0;
								uart1_tx_request_agency_info = 1;
						}
				}
				else if(car_select_mode)   			// TX :  구매 2. 차 선택 UI 출력
				{
						if( order_is_full ) // 대리점이 포화상태면 메뉴출력
						{
							if(garage_full[i] != '\0')
									LPC_UART0->THR = garage_full[i++];	
							else 
							{ 
									LPC_UART0->THR = '\0';
									uart0_tx_menu = 1;
									i = 0; 
							}
						}
						else
						{
							if(car_select[i] != '\0')
							{
									if(car_select[i] == 'a')  UART_SendByte( LPC_UART0, garage_num);
									else if(car_select[i] == 'b')  UART_SendByte( LPC_UART0, making_tim_num);
									else  UART_SendByte( LPC_UART0, car_select[i]);
									i++;
							}
							else
									i = 0;
						}
				}
				else if(set_password_mode) 					// TX : 구매 3. 고객 비밀번호 설정 UI 출력 			
				{
						if(is_setting)
						{
							//setting 중일때는 입력한 문자가 출력하도록 
						}
						else
						{
							if(set_password[i] != '\0')
								LPC_UART0->THR = set_password[i++];
							else
									i = 0;
						}
				}
				else if(tim_select_mode)									// TX : 구매 4. 공장 선택 UI 출력 
				{
						if( tim_is_full ) // 선택한 타이머가 가동중인경우
						{
							if(tim_full[i] != '\0')
									LPC_UART0->THR = tim_full[i++];	
							else 
							{ 
									LPC_UART0->THR = '\0';	 
									i = 0; tim_is_full = 0;
							}
						}
						else
						{
							 if(tim_select[i] != '\0')
									LPC_UART0->THR = tim_select[i++];
							else
								i = 0;
						}
				}
				else if(realtime_mode) 										// TX : 실시간 제작현황  UI 출력
				{
						if(realtime_flag_tim[3] == 0)
						{
							if(realtime_flag_tim[0] == 0)
							{
									if(realtime[i] != '\0')
									{
											if(realtime[i] == 'a') LPC_UART0->THR = '0';        				// 공장0
											else if(!tim0_realtime_flag.empty)																	// 가동 중 일때
											{
													if(realtime[i] == 'b') LPC_UART0->THR = tim0_fifo_user[tim0_front].userid + 48;
													else if(realtime[i] == 'c') LPC_UART0->THR = tim0_fifo_user[tim0_front].carname;
													else if(realtime[i] == 'd') LPC_UART0->THR = tim0_fifo_user[tim0_front].cost / 10 + 48;
													else if(realtime[i] == 'e') LPC_UART0->THR = tim0_fifo_user[tim0_front].cost % 10 + 48;            // 고객 정보
													else if(realtime[i] == 'g')
													{
														 if(tim0_realtime_flag.percent0)  LPC_UART0->THR = '_';
														else if(tim0_realtime_flag.percent25)  LPC_UART0->THR = '*';
														else if(tim0_realtime_flag.percent50)  LPC_UART0->THR = '*';
														else if(tim0_realtime_flag.percent75)  LPC_UART0->THR = '*';                    															
													}
													else if(realtime[i] == 'h')
													{
														 if(tim0_realtime_flag.percent0)  LPC_UART0->THR = '_';
														else if(tim0_realtime_flag.percent25)  LPC_UART0->THR = '_';
														else if(tim0_realtime_flag.percent50)  LPC_UART0->THR = '*';
														else if(tim0_realtime_flag.percent75)  LPC_UART0->THR = '*';
													}
													else if(realtime[i] == 'i')
													{
														 if(tim0_realtime_flag.percent0)  LPC_UART0->THR = '_';
														else if(tim0_realtime_flag.percent25)  LPC_UART0->THR = '_';
														else if(tim0_realtime_flag.percent50)  LPC_UART0->THR = '_';
														else if(tim0_realtime_flag.percent75)  LPC_UART0->THR = '*';
													}
													else if(realtime[i] == 'j')
													{
														 if(tim0_realtime_flag.percent0)  LPC_UART0->THR = '_';
														else if(tim0_realtime_flag.percent25)  LPC_UART0->THR = '_';
														else if(tim0_realtime_flag.percent50)  LPC_UART0->THR = '_';
														else if(tim0_realtime_flag.percent75)  LPC_UART0->THR = '_';																											// 제작률
													}
													else LPC_UART0->THR = realtime[i];
											}
											else																																							// 가동중이지 않을 때 
											{
												if(realtime[i] == 'b' || realtime[i] == 'c' || realtime[i] == 'd' || realtime[i] == 'e' || realtime[i] == 'g' || realtime[i] == 'h' || realtime[i] == 'i' || realtime[i] == 'j' || realtime[i] == '0' || realtime[i] == 'S')  
													LPC_UART0->THR = '-';
												else LPC_UART0->THR = realtime[i];
											}
											i++;
									}
									else 
									{
										i = 0; realtime_flag_tim[0] = 1; 
										LPC_UART0->THR = '\0'; 
									}
							}
							else if(realtime_flag_tim[1] == 0)
							{
									if(realtime[i] != '\0')
									{
											if(realtime[i] == 'a') LPC_UART0->THR = '1';										// 공장1
											else if(!tim1_realtime_flag.empty)																			// 가동 중 일때
											{
													if(realtime[i] == 'b') LPC_UART0->THR = tim1_fifo_user[tim1_front].userid + 48;
													else if(realtime[i] == 'c') LPC_UART0->THR = tim1_fifo_user[tim1_front].carname;
													else if(realtime[i] == 'd') LPC_UART0->THR = tim1_fifo_user[tim1_front].cost / 10 + 48;
													else if(realtime[i] == 'e') LPC_UART0->THR = tim1_fifo_user[tim1_front].cost % 10 + 48;						// 고객 정보
													else if(realtime[i] == 'g')
													{
														 if(tim1_realtime_flag.percent0)  LPC_UART0->THR = '_';
														else if(tim1_realtime_flag.percent25)  LPC_UART0->THR = '*';
														else if(tim1_realtime_flag.percent50)  LPC_UART0->THR = '*';
														else if(tim1_realtime_flag.percent75)  LPC_UART0->THR = '*';
													}
													else if(realtime[i] == 'h')
													{
														 if(tim1_realtime_flag.percent0)  LPC_UART0->THR = '_';
														else if(tim1_realtime_flag.percent25)  LPC_UART0->THR = '_';
														else if(tim1_realtime_flag.percent50)  LPC_UART0->THR = '*';
														else if(tim1_realtime_flag.percent75)  LPC_UART0->THR = '*';
													}
													else if(realtime[i] == 'i')
													{
														 if(tim1_realtime_flag.percent0)  LPC_UART0->THR = '_';
														else if(tim1_realtime_flag.percent25)  LPC_UART0->THR = '_';
														else if(tim1_realtime_flag.percent50)  LPC_UART0->THR = '_';
														else if(tim1_realtime_flag.percent75)  LPC_UART0->THR = '*';
													}
													else if(realtime[i] == 'j')
													{
														 if(tim1_realtime_flag.percent0)  LPC_UART0->THR = '_';
														else if(tim1_realtime_flag.percent25)  LPC_UART0->THR = '_';
														else if(tim1_realtime_flag.percent50)  LPC_UART0->THR = '_';
														else if(tim1_realtime_flag.percent75)  LPC_UART0->THR = '_';																										// 제작률
													}
													else LPC_UART0->THR = realtime[i];
											}
											else																																								// 가동 중 이지 않을 때 
											{
												if(realtime[i] == 'b' || realtime[i] == 'c' || realtime[i] == 'd' || realtime[i] == 'e' || realtime[i] == 'g' || realtime[i] == 'h' || realtime[i] == 'i' || realtime[i] == 'j' || realtime[i] == '0' || realtime[i] == 'S')  
													LPC_UART0->THR = '-';
												else LPC_UART0->THR = realtime[i];
											}
											i++;
									}
									else 
									{
										i = 0; realtime_flag_tim[1] = 1; 
										LPC_UART0->THR = '\0'; 
									}
							}
							else if(realtime_flag_tim[2] == 0)
							{
									if(realtime[i] != '\0')
									{
											if(realtime[i] == 'a') LPC_UART0->THR = '2';								// 공장 2
											else if(!tim2_realtime_flag.empty)																	// 가동 중 일때 
											{
													if(realtime[i] == 'b') LPC_UART0->THR = tim2_fifo_user[tim2_front].userid + 48;
													else if(realtime[i] == 'c') LPC_UART0->THR = tim2_fifo_user[tim2_front].carname;
													else if(realtime[i] == 'd') LPC_UART0->THR = tim2_fifo_user[tim2_front].cost / 10 + 48;
													else if(realtime[i] == 'e') LPC_UART0->THR = tim2_fifo_user[tim2_front].cost % 10 + 48;									// 유저 정보 
													else if(realtime[i] == 'g')
													{
														 if(tim2_realtime_flag.percent0)  LPC_UART0->THR = '_';
														else if(tim2_realtime_flag.percent25)  LPC_UART0->THR = '*';
														else if(tim2_realtime_flag.percent50)  LPC_UART0->THR = '*';
														else if(tim2_realtime_flag.percent75)  LPC_UART0->THR = '*';
													}
													else if(realtime[i] == 'h')
													{
														 if(tim2_realtime_flag.percent0)  LPC_UART0->THR = '_';
														else if(tim2_realtime_flag.percent25)  LPC_UART0->THR = '_';
														else if(tim2_realtime_flag.percent50)  LPC_UART0->THR = '*';
														else if(tim2_realtime_flag.percent75)  LPC_UART0->THR = '*';
													}
													else if(realtime[i] == 'i')
													{
														 if(tim2_realtime_flag.percent0)  LPC_UART0->THR = '_';
														else if(tim2_realtime_flag.percent25)  LPC_UART0->THR = '_';
														else if(tim2_realtime_flag.percent50)  LPC_UART0->THR = '_';
														else if(tim2_realtime_flag.percent75)  LPC_UART0->THR = '*';
													}
													else if(realtime[i] == 'j')
													{
														 if(tim2_realtime_flag.percent0)  LPC_UART0->THR = '_';
														else if(tim2_realtime_flag.percent25)  LPC_UART0->THR = '_';
														else if(tim2_realtime_flag.percent50)  LPC_UART0->THR = '_';
														else if(tim2_realtime_flag.percent75)  LPC_UART0->THR = '_';																													// 제작률
													}
													else LPC_UART0->THR = realtime[i];
											}
											else 																																						// 가동 중 이지 않을 때 
											{
												if(realtime[i] == 'b' || realtime[i] == 'c' || realtime[i] == 'd' || realtime[i] == 'e' || realtime[i] == 'g' || realtime[i] == 'h' || realtime[i] == 'i' || realtime[i] == 'j' || realtime[i] == '0' || realtime[i] == 'S')  
													LPC_UART0->THR = '-';
												else LPC_UART0->THR = realtime[i];
											}
											i++;
									}
									else 
									{
										i = 0; realtime_flag_tim[2] = 1; 
										LPC_UART0->THR = '\0'; 
									}
							}
							else if(realtime_flag_tim[3] == 0)
							{
									if(realtime[i] != '\0')
									{
											if(realtime[i] == 'a') LPC_UART0->THR = '3';							// 공장 3
											else if(!tim3_realtime_flag.empty)																// 가동중 일 때 
											{
													if(realtime[i] == 'b') LPC_UART0->THR = tim3_fifo_user[tim3_front].userid + 48;
													else if(realtime[i] == 'c') LPC_UART0->THR = tim3_fifo_user[tim3_front].carname;
													else if(realtime[i] == 'd') LPC_UART0->THR = tim3_fifo_user[tim3_front].cost / 10 + 48;
													else if(realtime[i] == 'e') LPC_UART0->THR = tim3_fifo_user[tim3_front].cost % 10 + 48;											// 유저 정보
													else if(realtime[i] == 'g')
													{
														 if(tim3_realtime_flag.percent0)  LPC_UART0->THR = '_';
														else if(tim3_realtime_flag.percent25)  LPC_UART0->THR = '*';
														else if(tim3_realtime_flag.percent50)  LPC_UART0->THR = '*';
														else if(tim3_realtime_flag.percent75)  LPC_UART0->THR = '*';
													}
													else if(realtime[i] == 'h')
													{
														 if(tim3_realtime_flag.percent0)  LPC_UART0->THR = '_';
														else if(tim3_realtime_flag.percent25)  LPC_UART0->THR = '_';
														else if(tim3_realtime_flag.percent50)  LPC_UART0->THR = '*';
														else if(tim3_realtime_flag.percent75)  LPC_UART0->THR = '*';
													}
													else if(realtime[i] == 'i')
													{
														 if(tim3_realtime_flag.percent0)  LPC_UART0->THR = '_';
														else if(tim3_realtime_flag.percent25)  LPC_UART0->THR = '_';
														else if(tim3_realtime_flag.percent50)  LPC_UART0->THR = '_';
														else if(tim3_realtime_flag.percent75)  LPC_UART0->THR = '*';
													}
													else if(realtime[i] == 'j')
													{
														 if(tim3_realtime_flag.percent0)  LPC_UART0->THR = '_';
														else if(tim3_realtime_flag.percent25)  LPC_UART0->THR = '_';
														else if(tim3_realtime_flag.percent50)  LPC_UART0->THR = '_';
														else if(tim3_realtime_flag.percent75)  LPC_UART0->THR = '_';																															// 제작률
													}
													else LPC_UART0->THR = realtime[i];
											}
											else																																						// 가동 중이지 않을 때 
											{
												if(realtime[i] == 'b' || realtime[i] == 'c' || realtime[i] == 'd' || realtime[i] == 'e' || realtime[i] == 'g' || realtime[i] == 'h' || realtime[i] == 'i' || realtime[i] == 'j' || realtime[i] == '0' || realtime[i] == 'S')  
													LPC_UART0->THR = '-';
												else LPC_UART0->THR = realtime[i];
											}
											i++;
									}
									else 
									{
										i = 0; realtime_flag_tim[3] = 1; 
										LPC_UART0->THR = '\0'; 
									}
							}
						}
						else																																											// 전부 출력시 Menu로 돌아감
						{
								realtime_flag_tim[0] = realtime_flag_tim[1] =  realtime_flag_tim[2] =  realtime_flag_tim[3] =  0;
								i = 0;
								uart0_tx_menu = 1;

						}
				}
		}
		else if((int_status & 0x0000000F) == 4) // RBR Interrupt check 
		{
				status = UART_GetLineStatus(LPC_UART0); // LSR

				if( ( status & 0x8E ) == 0x00 ) // No ERROR 
						uart0_rx_data = UART_ReceiveByte( LPC_UART0);

				uart0_rx_eflg =1;
		}
}


void UART1_IRQHandler(void){			 	// UART 1 : 상대 보드와  연결 && 레지스터만 번갈아사용 
		int32_t int_status;
		int32_t status;
		int_status = LPC_UART1->IIR;
	
		 if ((int_status & 0x0000000F) == 2) 		// THRE interrupt check
    {
					USER user;

					if (tim0_realtime_flag.percent100) user = tim0_fifo_user[tim0_front];																			// 완성된 차량에 해당하는 고객정보 갱신
					else if (tim1_realtime_flag.percent100) user = tim1_fifo_user[tim1_front];
					else if (tim2_realtime_flag.percent100) user = tim2_fifo_user[tim2_front];
					else if (tim3_realtime_flag.percent100) user = tim3_fifo_user[tim3_front];
					else return; 

					if (transfer_idx >= 0 && transfer_idx <= 5) LPC_UART1->THR = user.password[transfer_idx++];			// 고객정보 전송시작
					else if (transfer_idx >= 6 && transfer_idx <= 9) 
					{
							switch (transfer_idx) 
							{
									case 6: LPC_UART1->THR = user.userid ; break;
									case 7: LPC_UART1->THR = user.carname; break;
									case 8: LPC_UART1->THR = user.cost ; break;
									case 9: LPC_UART1->THR = '\0'; break;
							}
							transfer_idx++;
					} 
					else 																																																																								// 고객정보 전송끝, 공장 EMPTY 처리
					{
							if (tim0_realtime_flag.percent100) { tim0_realtime_flag.percent100 = 0; tim0_realtime_flag.empty = 1;}
							else if (tim1_realtime_flag.percent100) { tim1_realtime_flag.percent100 = 0; tim1_realtime_flag.empty = 1;}
							else if (tim2_realtime_flag.percent100) { tim2_realtime_flag.percent100 = 0; tim2_realtime_flag.empty = 1;}
							else if (tim3_realtime_flag.percent100) { tim3_realtime_flag.percent100 = 0; tim3_realtime_flag.empty = 1;}
							transfer_idx = 0;
							making_tim_num--;
					}
		}
		if((int_status & 0x0000000F) == 4) // RBR Interrupt check 
		{
				status = LPC_UART1->LSR; // LSR

				if( ( status & 0x8E ) == 0x00 ) // No ERROR 
						uart1_rx_data = LPC_UART1->RBR;

				if(uart1_rx_data >= '0' && uart1_rx_data <= '4') 
				{
						garage_num = uart1_rx_data;																																															// 대리점에서 현재 차고지 수 수신 
					
						if( ( garage_num - 48 ) + ( making_tim_num - 48 ) >= 4 ) order_is_full = 1;														// 대리점 포화시 FLAG처리 
						else order_is_full = 0;
					
						uart0_tx_car_select =1;
				}
		}

}


void TIMER0_IRQHandler(void) {			//  TIMER 0 ( 공장 0 ) : 레지스터 위주로 제어	
	
		int32_t int_status = LPC_TIM0->IR;


		if((int_status & 0x0000000F) == 1 ) 								// MR0 ( 차량 제작 기간의 25% )
		{
			LED_On(0);
			tim0_realtime_flag.percent0 = 0;
			tim0_realtime_flag.percent25 = 1;
		}
		else if ( (int_status & 0x0000000F) == 2 )				 // MR1  ( 차량 제작 기간의 50% )
		{
			LED_On(1);
			tim0_realtime_flag.percent25 = 0;
			tim0_realtime_flag.percent50 = 1;
		}
		else if((int_status & 0x0000000F) == 4) 					// MR2  ( 차량 제작 기간의 75% )
		{
			LED_Off(0);
			tim0_realtime_flag.percent50 = 0;
			tim0_realtime_flag.percent75 = 1;
		}
		else if ( (int_status & 0x0000000F) == 8)			 // MR3 . Stop ( 차량 제작 기간의 100% , 완성  )
		{
			LED_Off(1);
			tim0_realtime_flag.percent75 = 0;
			tim0_realtime_flag.percent100 = 1;
			LPC_TIM0->TC = 0; 
			LPC_TIM0->PC = 0; 
			LPC_TIM0->TCR = 0x00000002; 
			uart1_tx_userinfo = 1;																	// 대리점으로 유저정보 송신
		}
			NVIC->ICPR[0] |= (1<<1);
			LPC_TIM0->IR = 0xFFFFFFFF; 
		
		if(menu_mode&&is_realtime) uart0_tx_realtime =1; 	// MR마다 실시간 현황 출력 (메뉴모드시)
}

void TIMER1_IRQHandler(void) {					//  TIMER 1 ( 공장 1 ) : 함수 위주로 제어
	
		int32_t int_status = LPC_TIM1->IR;

	
		if((int_status & 0x0000000F) == 1) 											// MR0  ( 차량 제작 기간의 25% )
		{
			LED_On(2);
			tim1_realtime_flag.percent0 = 0;
			tim1_realtime_flag.percent25 = 1;
		}
		else if ( (int_status & 0x0000000F) == 2 )						// MR1  ( 차량 제작 기간의 50% )
		{
			LED_On(3);
			tim1_realtime_flag.percent25 = 0;
			tim1_realtime_flag.percent50 = 1;
		}
		else if((int_status & 0x0000000F) == 4) 							// MR2  ( 차량 제작 기간의 75% )
		{
			LED_Off(2);
			tim1_realtime_flag.percent50 = 0;
			tim1_realtime_flag.percent75 = 1;
		}
		else if ( (int_status & 0x0000000F) == 8 )					 // MR3 . Stop ( 차량 제작 기간의 100% , 완성  )
		{
			LED_Off(3);
			tim1_realtime_flag.percent75 = 0;
			tim1_realtime_flag.percent100 = 1;
			TIM_ResetCounter(LPC_TIM1); 
			TIM_Cmd(LPC_TIM1,DISABLE); 
			uart1_tx_userinfo = 1;																			// 대리점으로 유저정보 송신
		}
			NVIC_ClearPendingIRQ (TIMER1_IRQn);  
			LPC_TIM1->IR = 0xFFFFFFFF; 
		
			if(menu_mode&&is_realtime) uart0_tx_realtime =1;		// MR마다 실시간 현황 출력 (메뉴모드시)
}

void TIMER2_IRQHandler(void) {					//  TIMER 2 ( 공장 2 ) : 레지스터 위주로 제어
	
	
		int32_t int_status = LPC_TIM2->IR;

	
		if((int_status & 0x0000000F) == 1)											 // MR0  ( 차량 제작 기간의 25% )
		{
			LED_On(4);
			tim2_realtime_flag.percent0 = 0;
			tim2_realtime_flag.percent25 = 1;
		}
		else if ( (int_status & 0x0000000F) == 2 ) 						// MR1  ( 차량 제작 기간의 50% )
		{
			LED_On(5);
			tim2_realtime_flag.percent25 = 0;
			tim2_realtime_flag.percent50 = 1;
		}
		else if((int_status & 0x0000000F) == 4) 								// MR2  ( 차량 제작 기간의 75% )
		{
			LED_Off(4);
			tim2_realtime_flag.percent50 = 0;
			tim2_realtime_flag.percent75 = 1;
		}
		else if ( (int_status & 0x0000000F) == 8 ) 						// MR3 . Stop ( 차량 제작 기간의 100% , 완성  )
		{
			LED_Off(5);
			tim2_realtime_flag.percent75 = 0;
			tim2_realtime_flag.percent100 = 1;
			LPC_TIM2->TC = 0; 
			LPC_TIM2->PC = 0; 
			LPC_TIM2->TCR = 0x000000002; 
			uart1_tx_userinfo = 1;																				// 대리점으로 유저정보 송신
		}
			NVIC->ICPR[0] |= (1<<3);
			LPC_TIM2->IR = 0xFFFFFFFF; 
			
			if(menu_mode&&is_realtime) uart0_tx_realtime =1; // MR마다 실시간 현황 출력 (메뉴모드시)
}

void TIMER3_IRQHandler(void) {						//  TIMER 3 ( 공장 3 ) : 함수 위주로 제어
	
		int32_t int_status = LPC_TIM3->IR;

	
		if((int_status & 0x0000000F) == 1) 													// MR0  ( 차량 제작 기간의 25% )
		{
			LED_On(6);
			tim3_realtime_flag.percent0 = 0;
			tim3_realtime_flag.percent25 = 1;
		}
		else if ( (int_status & 0x0000000F) == 2 ) 							// MR1  ( 차량 제작 기간의 50% )
		{
			LED_On(7);
			tim3_realtime_flag.percent25 = 0;
			tim3_realtime_flag.percent50 = 1;
		}
		else if((int_status & 0x0000000F) == 4)									 // MR2  ( 차량 제작 기간의 75% )
		{
			LED_Off(6);
			tim3_realtime_flag.percent50 = 0;
			tim3_realtime_flag.percent75 = 1;
		}
		else if ( (int_status & 0x0000000F) == 8 ) 							// MR3 . Stop ( 차량 제작 기간의 100% , 완성  )
		{
			LED_Off(7);
			tim3_realtime_flag.percent75 = 0;
			tim3_realtime_flag.percent100 = 1;
			TIM_ResetCounter(LPC_TIM3); 
			TIM_Cmd(LPC_TIM3,DISABLE); 
			uart1_tx_userinfo = 1;																					// 대리점으로 유저정보 송신
		}
			NVIC_ClearPendingIRQ (TIMER3_IRQn);  
			LPC_TIM3->IR = 0xFFFFFFFF;
		
			if(menu_mode&&is_realtime) uart0_tx_realtime =1;  // MR마다 실시간 현황 출력 (메뉴모드시)

}

// ================================================================================================================================================================================

// UART, TIMER 관련 함수 정의 ( UART0, TIM1, TIM3 - 함수위주 / UART1, TIM0, TIM2 - 레지스터 위주 ) 

void UART0_Init_ByFunc() {
		
			//Power & CLK & Pin Enable 
			LPC_SC->PCONP |= (1 << 3);   								// Bit 3 -> 1 for UART0 Power On 

			LPC_SC->PCLKSEL0 &= ~(3 << 6);  
			LPC_SC->PCLKSEL0 |= (0 << 6);   						// Bit 6&7 00 for CCLK/4


			LPC_PINCON->PINSEL0 &= ~( 3<< 4 ); 
			LPC_PINCON->PINSEL0 |= ( 1<< 4 );  			// Bit 5&4 01 for TXD0 Select
		
			LPC_PINCON->PINSEL0 &= ~( 3<< 6); 
			LPC_PINCON->PINSEL0 |= ( 1 << 6 ); 			// Bit 7&6 01 for RXD0 Select
		
			NVIC->ICPR[0] |= ( 1 << 5 );										 // Clear Pending for UART0
			NVIC->ISER[0] |= (1 << 5 ); 											// Interrupt Enable for UART0
	
				//Set BaudRate 115200
			UART_config_struct.Baud_rate = 115200; 	
			UART_config_struct.Parity = UART_PARITY_NONE;
			UART_config_struct.Databits = UART_DATABIT_8 ;
			UART_config_struct.Stopbits = UART_STOPBIT_1 ;
		
			UART_Init( LPC_UART0, &UART_config_struct );
		
			UART_FIFOConfigStructInit( &UART_FIFO_config_struct );
			UART_FIFOConfig(  LPC_UART0, &UART_FIFO_config_struct );
			
			UART_IntConfig( LPC_UART0, UART_INTCFG_RBR, ENABLE ); 
			UART_IntConfig( LPC_UART0, UART_INTCFG_THRE, ENABLE );
			
			UART_TxCmd(LPC_UART0, ENABLE);
}

void UART1_Init_ByReg() {
	
		//Power & CLK & Pin Enable 
		LPC_SC->PCONP |= (1<<4);             
	
		LPC_SC->PCLKSEL0 &= ~( 3 << 8 );  
		LPC_SC->PCLKSEL0 |= (0 << 8);  						 // Bit 8&9 00 for CCLK/4
       
		LPC_PINCON->PINSEL4 &= ~( 3 << 0);       
		LPC_PINCON->PINSEL4 |= ( 2 << 0);					// Bit 1&0 10 for TXD1 Select
       
		LPC_PINCON->PINSEL4 &= ~( 3 << 2);       	
		LPC_PINCON->PINSEL4 |= ( 2 << 2);					// Bit 3&2 10 for RXD1 Select
	
		NVIC->ICPR[0] |= ( 1 << 6 );											 // Clear Pending for UART0
		NVIC->ISER[0] |= (1 << 6 ); 												// Interrupt Enable for UART0
          
    //Set BaudRate 115200
		LPC_UART1->LCR = 0x83; // DLAB=1 for DLL, DLM ( no Parity, 1stop bit, 8bit ) 

		LPC_UART1->DLL = 0x09; 
		LPC_UART1->DLM = 0x00;
		LPC_UART1->FDR = 0x21; 
		

		LPC_UART1->LCR = 0x03; 
		LPC_UART1->FCR = 0x06; 
			
			
		LPC_UART1->IER = 0x03; 
		LPC_UART1->FCR = 0x01;
		LPC_UART1->TER = 0x80;
		
}



void TIMER0_Init_ByReg(){  																						// 가장 느린 공장 타이머0 ( PCLK = CCLK / 8 )
		
	
		//Power & CLK
		LPC_SC->PCONP |= (1<<1);  
	
		LPC_SC->PCLKSEL0 &= ~(3 << 2);  
		LPC_SC->PCLKSEL0 |= (3<<2); 
	
		NVIC->ICPR[0] |= (1<<1); 
		NVIC->ISER[0] |= (1<<1); 
	
		// initialization
		LPC_TIM0->IR = 0xFFFFFFFF;		
		LPC_TIM0->TC = 0; 
		LPC_TIM0->PC = 0; 
		LPC_TIM0->PR = 0; 
	
		LPC_TIM0->TCR = 0x00000002; 
		LPC_TIM0->CTCR = 0x00000000;
		LPC_TIM0->PR = (SystemCoreClock / 1000000); // = 100
			//PCLK = 12.5Mhz --> TC 주파수 = PCLK / 101 = 123762hz
			//TC주기 8.08usec
}

void TIMER0_MakeCar_ByReg(char CAR_NAME){   				// 	제작기간에 따라 MR값 변경 & 타이머 (공장) 가동 
	
		int one_sec = 123762; // 8usec * 123762 = 1초
		int finish_time; 
				
		switch (CAR_NAME) {
					case S3:
            finish_time = S3 * one_sec;
            break;
					case S6: 
							finish_time = S6 * one_sec;
							break;
					case S9: 
							finish_time = S9 * one_sec;
							break;
					default:
							finish_time = 10 * one_sec;
							break;
			}

    LPC_TIM0->MR3 = (uint32_t)finish_time;
    LPC_TIM0->MR2 = (uint32_t)(0.75 * finish_time);
    LPC_TIM0->MR1 = (uint32_t)(0.5 * finish_time);
    LPC_TIM0->MR0 = (uint32_t)(0.25 * finish_time);

    LPC_TIM0->MCR = 0x00000A49;//  0x0000A49; // MR3때에만 Stop, 모든 MR Interrupt Enable
    LPC_TIM0->TCR = 0x00000001;

		tim0_realtime_flag.empty = 0;
		tim0_realtime_flag.percent0 = 1;
}
	

void TIMER1_Init_ByFunc() {																					// 공장1 ( 공장0에 비해 2배 빠름 ) ( PCLK = CCLK / 4 )

		NVIC_ClearPendingIRQ (TIMER1_IRQn); 
		NVIC_EnableIRQ (TIMER1_IRQn); 
	
		TIM1_ConfigStruct.PrescaleOption = TIM_PRESCALE_TICKVAL;
		TIM1_ConfigStruct.PrescaleValue = 101; // PR=100 

		
		TIM_Init(LPC_TIM1, TIM_TIMER_MODE, &TIM1_ConfigStruct); // CCLK/4로 설정 ( TIM_Init함수 ) 
}

void TIMER1_MakeCar_ByFunc(char CAR_NAME){					// 	제작기간에 따라 MR값 변경 & 타이머 (공장) 가동 

	
		int one_sec = 123762; // 8usec * 123762 = 1초
		int finish_time; 
		
		switch (CAR_NAME) {
					case S3: 
            finish_time = S3 * one_sec;
            break;
					case S6: 
							finish_time = S6 * one_sec;
							break;
					case S9:
							finish_time = S9 * one_sec;
							break;
					default:
							finish_time = 10 * one_sec; // Default to 10 seconds
							break;
			}
		
			// MR3때에만 Stop, 모든 MR Interrupt Enable
			
			TIM1_MatchConfigStruct.MatchChannel = 3;
			TIM1_MatchConfigStruct.IntOnMatch = TRUE;
			TIM1_MatchConfigStruct.StopOnMatch = TRUE;
			TIM1_MatchConfigStruct.MatchValue = (uint32_t)finish_time;
			TIM_ConfigMatch(LPC_TIM1, &TIM1_MatchConfigStruct);
			
			TIM1_MatchConfigStruct.MatchChannel = 2;
			TIM1_MatchConfigStruct.IntOnMatch = TRUE;
			TIM1_MatchConfigStruct.StopOnMatch = FALSE;
			TIM1_MatchConfigStruct.MatchValue = (uint32_t)(0.75 * finish_time);
			TIM_ConfigMatch(LPC_TIM1, &TIM1_MatchConfigStruct);
			
			TIM1_MatchConfigStruct.MatchChannel = 1;
			TIM1_MatchConfigStruct.IntOnMatch = TRUE;
			TIM1_MatchConfigStruct.StopOnMatch = FALSE;
			TIM1_MatchConfigStruct.MatchValue = (uint32_t)(0.5 * finish_time);
			TIM_ConfigMatch(LPC_TIM1, &TIM1_MatchConfigStruct);
			
			TIM1_MatchConfigStruct.MatchChannel = 0;
			TIM1_MatchConfigStruct.IntOnMatch = TRUE;
			TIM1_MatchConfigStruct.StopOnMatch = FALSE;
			TIM1_MatchConfigStruct.MatchValue = (uint32_t)(0.25 * finish_time);
			TIM_ConfigMatch(LPC_TIM1, &TIM1_MatchConfigStruct);

			TIM_Cmd(LPC_TIM1, ENABLE); 
			
			tim1_realtime_flag.empty = 0;
			tim1_realtime_flag.percent0 = 1;
}

void TIMER2_Init_ByReg(){																// 공장2 ( 공장0에 비해 4배 빠름 ) ( PCLK = CCLK / 2 )
		
		//Power & CLK
		LPC_SC->PCONP |= (1<< 22);  
	
		LPC_SC->PCLKSEL1 &= ~(3 << 12);  
		LPC_SC->PCLKSEL1 |= (2 << 12); 
	
		NVIC->ICPR[0] |= (1<<3); 
		NVIC->ISER[0] |= (1<<3); 
	

		LPC_TIM2->IR = 0xFFFFFFFF;		 
		LPC_TIM2->TC = 0; 
		LPC_TIM2->PC = 0; 
		LPC_TIM2->PR = 0; 
	
		LPC_TIM2->TCR = 0x00000002; // TC Reset
		LPC_TIM2->CTCR = 0x00000000; // Timer mode
		LPC_TIM2->PR = (SystemCoreClock / 1000000); // = 100
	
}

void TIMER2_MakeCar_ByReg(char CAR_NAME){					// 	제작기간에 따라 MR값 변경 & 타이머 (공장) 가동 
	
		int one_sec = 123762;
		int finish_time; 
				
		switch (CAR_NAME) {
					case S3: 
            finish_time = S3 * one_sec;
            break;
					case S6:
							finish_time = S6 * one_sec;
							break;
					case S9: 
							finish_time = S9 * one_sec;
							break;
					default:
							finish_time = 10 * one_sec;
							break;
			}

    LPC_TIM2->MR3 = finish_time;
    LPC_TIM2->MR2 = (uint32_t) (0.75 * finish_time);
    LPC_TIM2->MR1 = (uint32_t) (0.5 * finish_time);
    LPC_TIM2->MR0 = (uint32_t) (0.25 * finish_time);

    LPC_TIM2->MCR = 0x00000A49; // MR3때에만 Stop, 모든 MR Interrupt Enable
    LPC_TIM2->TCR = 0x00000001; 
			
		tim2_realtime_flag.empty = 0;
		tim2_realtime_flag.percent0 = 1;
}

void TIMER3_Init_ByFunc() {																		// 공장3 ( 공장0에 비해 8배 빠름 ) ( PCLK = CCLK )
	
		NVIC_ClearPendingIRQ (TIMER3_IRQn); 
		NVIC_EnableIRQ (TIMER3_IRQn); 
	
		TIM3_ConfigStruct.PrescaleOption = TIM_PRESCALE_TICKVAL;
		TIM3_ConfigStruct.PrescaleValue = 101; // PR=100 

		// initialization
		TIM_Init(LPC_TIM3, TIM_TIMER_MODE, &TIM3_ConfigStruct); //  PCLK = CCLK로 설정 (****TIM_Init함수 수정 ( lpc17xx.timer.c) ****) 
}

void TIMER3_MakeCar_ByFunc(char CAR_NAME){					// 	제작기간에 따라 MR값 변경 & 타이머 (공장) 가동 
	
		int one_sec = 123762; 
		int finish_time; 
				
		switch (CAR_NAME) {
					case S3: 
            finish_time = S3 * one_sec;
            break;
					case S6: 
							finish_time = S6 * one_sec;
							break;
					case S9: 
							finish_time = S9 * one_sec;
							break;
					default:
							finish_time = 10 * one_sec;
							break;
			}
		
			// MR3때에만 Stop, 모든 MR Interrupt Enable
			
			TIM3_MatchConfigStruct.MatchChannel = 3;
			TIM3_MatchConfigStruct.IntOnMatch = TRUE;
			TIM3_MatchConfigStruct.StopOnMatch = TRUE;
			TIM3_MatchConfigStruct.MatchValue = (uint32_t)finish_time;
			TIM_ConfigMatch(LPC_TIM3, &TIM3_MatchConfigStruct);
			
			TIM3_MatchConfigStruct.MatchChannel = 2;
			TIM3_MatchConfigStruct.IntOnMatch = TRUE;
			TIM3_MatchConfigStruct.StopOnMatch = FALSE;
			TIM3_MatchConfigStruct.MatchValue = (uint32_t) ( 0.75 * finish_time );
			TIM_ConfigMatch(LPC_TIM3, &TIM3_MatchConfigStruct);
			
			TIM3_MatchConfigStruct.MatchChannel = 1;
			TIM3_MatchConfigStruct.IntOnMatch = TRUE;
			TIM3_MatchConfigStruct.StopOnMatch = FALSE;
			TIM3_MatchConfigStruct.MatchValue = (uint32_t) ( 0.5 * finish_time );
			TIM_ConfigMatch(LPC_TIM3, &TIM3_MatchConfigStruct);
			
			TIM3_MatchConfigStruct.MatchChannel = 0;
			TIM3_MatchConfigStruct.IntOnMatch = TRUE;
			TIM3_MatchConfigStruct.StopOnMatch = FALSE;
			TIM3_MatchConfigStruct.MatchValue = (uint32_t) ( 0.25 * finish_time );
			TIM_ConfigMatch(LPC_TIM3, &TIM3_MatchConfigStruct);

			TIM_Cmd(LPC_TIM3, ENABLE); // TC Enable
			
			tim3_realtime_flag.empty = 0;
			tim3_realtime_flag.percent0 = 1;
}

// ================================================================================================================================================================================

// 그 외 함수 

void setInterruptPriorities() {																				// Interrupt 우선순위 설정 
    NVIC_SetPriority(TIMER0_IRQn, 1);
    NVIC_SetPriority(TIMER1_IRQn, 2);
    NVIC_SetPriority(TIMER2_IRQn, 3);
    NVIC_SetPriority(TIMER3_IRQn, 4);
		NVIC_SetPriority(UART0_IRQn, 5);
    NVIC_SetPriority(UART1_IRQn, 6);
}

void uart0_set_mode(char mode_name)										 // UART0 : 설정한 모드 외  나머지 mode 0 
{ 

  menu_mode = agency_mode = car_select_mode = set_password_mode = tim_select_mode = realtime_mode = 0;
	if(mode_name == MENU_MODE) 
			menu_mode = 1;
	else if(mode_name == CAR_SELECT_MODE) 
			car_select_mode = 1;
	else if(mode_name == AGENCY_MODE) 
			agency_mode = 1;
	else if(mode_name == SET_PASSWORD_MODE) 
			set_password_mode = 1;
	else if(mode_name == TIM_SELECT_MODE) 
			tim_select_mode = 1;
	else if(mode_name == REALTIME_MODE) 
			realtime_mode = 1;
	
}

void uart1_set_mode(char mode_name)											// UART1 : 설정한 모드 외  나머지 mode 0 
{ 

	transfer_userinfo_mode = request_agencyinfo_mode = 0; 
  
	if(mode_name == TRANSFER_USERINFO_MODE) 
			transfer_userinfo_mode = 1;
	else if(mode_name == REQUEST_AGENCYINFO_MODE)
			request_agencyinfo_mode = 1;
	
}

// ================================================================================================================================================================================

void check_failed(uint8_t *file, uint32_t line)
{
            while(1);
}