#include <iostream>
#include <float.h>
#include <math.h>
#include <string>
#include <map>
#include <vector>
#include <functional>
#include <stdlib.h>
#include <string.h>
#include <curses.h>
#ifdef _WIN32
#undef getch
#undef ungetch
#include<conio.h>
#endif
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>

#include "ThostFtdcMdApi.h"

// ���Ƽ�����
#define KEYBOARD_UP 19 // 72
#define KEYBOARD_DOWN 20 // 80
#define KEYBOARD_LEFT 21 //75
#define KEYBOARD_RIGHT 22 // 77
#define KEYBOARD_REFRESH 28 // 12 ^L

typedef struct {
	char exchange_id[20];
	char exchange_name[100];
	char product_id[30];
	char product_name[100];
	double price;
	int quantity;
	int trade_volume;
	double high;
	double low;
	double settle;
	double prev_close;
	int openint;
	int prev_openint;
	double average_price;
	double buy_price;
	int buy_quantity;
	double sell_price;
	int sell_quantity;
	double open_price;
	double prev_settle;
	double min_movement;
	double high_limit;
	double low_limit;
	int precision;
	bool subscribed;
	char expired_date[20];
	int product_type;
	char product[20];
	double margin_ratio;
	int multiple;
	char tradingday[11];
} quotation_t;

#define CONNECTION_STATUS_DISCONNECTED	0
#define CONNECTION_STATUS_CONNECTED		1
#define CONNECTION_STATUS_LOGINOK		2
#define CONNECTION_STATUS_LOGINFAILED	3

std::vector<quotation_t> vquotes;
std::map<std::string, size_t> mquotes; // ��������Լ��==>vquotes�±�
int ConnectionStatus = CONNECTION_STATUS_DISCONNECTED;
char** instruments = NULL; // Ҫ���ĵĺ�Լ�б�
size_t instrument_count = 0;

// Basic
void init_screen();
int on_key_pressed(int ch);
void time_thread();
void work_thread();
void HandleTickTimeout();

// Main Board
void refresh_screen();
void display_title();
void display_status();
void display_quotation(const char* product_id);
int on_key_pressed_mainboard(int ch);
int move_forward_1_line();
int move_backward_1_line();
int scroll_left_1_column();
int scroll_right_1_column();
void focus_quotation(int index);


void post_task(std::function<void()> task);


// CTP
class CCTPMdSpiImp :public CThostFtdcMdSpi
{
public:
	CCTPMdSpiImp(CThostFtdcMdApi* pMdApi) :m_pMdApi(pMdApi) {}

	//������
	void OnFrontConnected() { post_task(std::bind(&CCTPMdSpiImp::HandleFrontConnected, this)); }

	//δ����
	void OnFrontDisconnected(int nReason) { post_task(std::bind(&CCTPMdSpiImp::HandleFrontDisconnected, this, nReason)); }

	//��¼Ӧ��
	void OnRspUserLogin(CThostFtdcRspUserLoginField* pRspUserLogin, CThostFtdcRspInfoField* pRspInfo, int nRequestID, bool bIsLast) { post_task(std::bind(&CCTPMdSpiImp::HandleRspUserLogin, this, *pRspUserLogin, *pRspInfo, nRequestID, bIsLast)); }

	//���������������֪ͨ
	void OnRtnDepthMarketData(CThostFtdcDepthMarketDataField* pDepthMarketData) { post_task(std::bind(&CCTPMdSpiImp::HandleRtnDepthMarketData, this, *pDepthMarketData)); }

	void HandleFrontConnected()
	{
		ConnectionStatus = CONNECTION_STATUS_CONNECTED;
		CThostFtdcReqUserLoginField Req;

		memset(&Req, 0x00, sizeof(Req));
		m_pMdApi->ReqUserLogin(&Req, 0);
	}

	void HandleFrontDisconnected(int nReason)
	{
		ConnectionStatus = CONNECTION_STATUS_DISCONNECTED;
		refresh_screen();
	}

	void HandleRspUserLogin(CThostFtdcRspUserLoginField& RspUserLogin, CThostFtdcRspInfoField& RspInfo, int nRequestID, bool bIsLast)
	{
		ConnectionStatus = CONNECTION_STATUS_LOGINOK;
		display_status();

		if (instrument_count == 0)
		{
			char* contracts[1];
			contracts[0] = "*";
			m_pMdApi->SubscribeMarketData(contracts, 1);
		}
		else
		{
			m_pMdApi->SubscribeMarketData(instruments, instrument_count);
		}
	}

	void HandleRtnDepthMarketData(CThostFtdcDepthMarketDataField& DepthMarketData)
	{
		std::map<std::string, size_t>::iterator iter;
		if ((iter = mquotes.find(DepthMarketData.InstrumentID)) == mquotes.end()) {
			// new
			quotation_t quot;
			memset(&quot, 0x00, sizeof(quot));
			strcpy(quot.product_id, DepthMarketData.InstrumentID);
			strcpy(quot.exchange_id, DepthMarketData.ExchangeID);
			quot.precision = 2;
			vquotes.push_back(quot);
			mquotes[DepthMarketData.InstrumentID] = vquotes.size() - 1;
			iter = mquotes.find(DepthMarketData.InstrumentID);
		}
		quotation_t& q = vquotes[iter->second];

		q.price = DepthMarketData.LastPrice;
		if (q.quantity != DepthMarketData.Volume)
			q.trade_volume = DepthMarketData.Volume - q.quantity;
		q.quantity = DepthMarketData.Volume;
		q.buy_price = DepthMarketData.BidPrice1;
		q.buy_quantity = DepthMarketData.BidVolume1;
		q.sell_price = DepthMarketData.AskPrice1;
		q.sell_quantity = DepthMarketData.AskVolume1;
		q.open_price = DepthMarketData.OpenPrice;
		q.high = DepthMarketData.HighestPrice;
		q.low = DepthMarketData.LowestPrice;
		q.high_limit = DepthMarketData.UpperLimitPrice;
		q.low_limit = DepthMarketData.LowerLimitPrice;
		q.settle = DepthMarketData.SettlementPrice;
		q.openint = DepthMarketData.OpenInterest;
		q.prev_openint = DepthMarketData.PreOpenInterest;
		q.prev_close = DepthMarketData.PreClosePrice;
		q.prev_settle = DepthMarketData.PreSettlementPrice;
		q.average_price = DepthMarketData.Volume ? DepthMarketData.Turnover / DepthMarketData.Volume : 0.0;
		strcpy(q.tradingday, DepthMarketData.TradingDay);

		display_quotation(DepthMarketData.InstrumentID);
	}

	CThostFtdcMdApi* m_pMdApi;
};


class semaphore
{
public:
	semaphore(int value = 1) :count(value) {}

	void wait()
	{
		std::unique_lock<std::mutex> lck(mt);
		if (--count < 0)//��Դ��������߳�
			cv.wait(lck);
	}

	void signal()
	{
		std::unique_lock<std::mutex> lck(mt);
		if (++count <= 0)//���̹߳��𣬻���һ��
			cv.notify_one();
	}

private:
	int count;
	std::mutex mt;
	std::condition_variable cv;
};


std::mutex _lock;
semaphore _sem;
std::vector< std::function<void()> > _vTasks;

typedef struct {
	char name[30];
	int width;
} column_item_t;

column_item_t column_items[]={
#define COL_SYMBOL			0		// ��Լ
	{"��Լ",		20},
#define COL_SYMBOL_NAME		1		// ����
	{"����",		20},
#define COL_CLOSE			2		// �ּ�
	{"�ּ�",		10},
#define COL_PERCENT			3		// �Ƿ�
	{"�Ƿ�",		10},
#define COL_VOLUME			4		// ����
	{"����",		10},
#define COL_TRADE_VOLUME	5		// ����
	{"����",		10},
#define COL_ADVANCE			6		// �ǵ�
	{"�ǵ�",		10},
#define COL_OPEN			7		// ����
	{"����",		10},
#define COL_HIGH			8		// ���
	{"���",		10},
#define COL_LOW				9		// ���
	{"���",		10},
#define COL_BID_PRICE		10		// ���
	{"���",		10},
#define COL_BID_VOLUME		11		// ����
	{"����",		10},
#define COL_ASK_PRICE		12		// ����
	{"����",		10},
#define COL_ASK_VOLUME		13		// ����
	{"����",		10},
#define COL_PREV_SETTLEMENT	14		// ���
	{"���",		10},
#define COL_SETTLEMENT		15		// ���
	{"���",		10},
#define COL_PREV_CLOSE		16		// ����
	{"����",		10},
#define COL_OPENINT			17		// ���
	{"���",		10},
#define COL_PREV_OPENINT	18		// ���
	{"���",		10},
#define COL_AVERAGE_PRICE	19		// ����
	{"����",		10},
#define COL_HIGH_LIMIT		20		// ��ͣ
	{"��ͣ",		10},
#define COL_LOW_LIMIT		21		// ��ͣ
	{"��ͣ",		10},
#define COL_TRADINGDAY		22		// ������
	{"������",		10}
};
std::vector<int> vcolumns;	// columns in order
std::map<int,bool> mcolumns;	// column select status

// Mainboard Curses
int curr_line=0,curr_col=1,max_lines,max_cols=7;
int curr_pos=0,curr_col_pos=2;

int main(int argc,char *argv[])
{
	if (argc != 2 && argc != 3)
	{
		std::cout << "usage:ctppriceview marketaddr [instrument,instrument ...]" << std::endl;
		std::cout << "example:ctppriceview tcp://180.168.146.187:10100" << std::endl;
		std::cout << "example:ctppriceview tcp://180.168.146.187:10100 rb2105,IF2101" << std::endl;
		return -1;
	}

	char* servaddr = argv[1];
	const char* p;
	
	// instruments
	if (argc == 3) {
		instrument_count =1;
		for (p = argv[2]; *p != '\0'; p++)
			if (*p == ',')
				instrument_count++;
	
		instruments = (char**)malloc(sizeof(char*)* instrument_count);
		char** instrument = instruments;
		char* token=NULL;
		p = argv[2];
		for (int i=0; i<instrument_count; i++) {
			if (token == NULL) {
				token = strtok(argv[2], ",");
			}
			else {
				token = strtok(NULL, ",");
			}
			*instrument++ = token;
		}
	}

	if (strstr(servaddr,"://") == NULL) {
		std::cout << "invalid addr format" << std::endl;
		return -1;
	}
	CThostFtdcMdApi* pMdApi = CThostFtdcMdApi::CreateFtdcMdApi();
	CCTPMdSpiImp Spi(pMdApi);
	pMdApi->RegisterSpi(&Spi);
	pMdApi->RegisterFront(servaddr);
	pMdApi->Init();


	// Init Columns
	mcolumns[COL_SYMBOL]=true;vcolumns.push_back(COL_SYMBOL);
	mcolumns[COL_SYMBOL_NAME]=false;vcolumns.push_back(COL_SYMBOL_NAME);
	mcolumns[COL_CLOSE]=true;vcolumns.push_back(COL_CLOSE);
	mcolumns[COL_PERCENT]=true;vcolumns.push_back(COL_PERCENT);
	mcolumns[COL_VOLUME]=true;vcolumns.push_back(COL_VOLUME);
	mcolumns[COL_TRADE_VOLUME]=true;vcolumns.push_back(COL_TRADE_VOLUME);
	mcolumns[COL_BID_PRICE]=true;vcolumns.push_back(COL_BID_PRICE);
	mcolumns[COL_BID_VOLUME]=true;vcolumns.push_back(COL_BID_VOLUME);
	mcolumns[COL_ASK_PRICE]=true;vcolumns.push_back(COL_ASK_PRICE);
	mcolumns[COL_ASK_VOLUME]=true;vcolumns.push_back(COL_ASK_VOLUME);
	mcolumns[COL_PREV_SETTLEMENT]=true;vcolumns.push_back(COL_PREV_SETTLEMENT);
	mcolumns[COL_ADVANCE]=true;vcolumns.push_back(COL_ADVANCE);
	mcolumns[COL_OPEN]=true;vcolumns.push_back(COL_OPEN);
	mcolumns[COL_HIGH]=true;vcolumns.push_back(COL_HIGH);
	mcolumns[COL_LOW]=true;vcolumns.push_back(COL_LOW);
	mcolumns[COL_HIGH_LIMIT] = true; vcolumns.push_back(COL_HIGH_LIMIT);
	mcolumns[COL_LOW_LIMIT] = true; vcolumns.push_back(COL_LOW_LIMIT);
	mcolumns[COL_AVERAGE_PRICE]=true;vcolumns.push_back(COL_AVERAGE_PRICE);
	mcolumns[COL_PREV_CLOSE]=true;vcolumns.push_back(COL_PREV_CLOSE);
	mcolumns[COL_OPENINT]=true;vcolumns.push_back(COL_OPENINT);
	mcolumns[COL_PREV_OPENINT]=true;vcolumns.push_back(COL_PREV_OPENINT);
	mcolumns[COL_SETTLEMENT]=true;vcolumns.push_back(COL_SETTLEMENT);
	mcolumns[COL_TRADINGDAY] = true; vcolumns.push_back(COL_TRADINGDAY);

	std::thread workthread(&work_thread);
	std::thread timerthread(&time_thread);

	// Idle
	int ch;
	while (1) {
#ifdef _WIN32
		ch = getch();
#else
		ch = getchar();
#endif
		if (ch == 224) {
#ifdef _WIN32
			ch = getch();
#else
			ch = getchar();
#endif
			switch (ch)
			{
			case 75: // ��
				ch = KEYBOARD_LEFT;
				break;
			case 80: // ��
				ch = KEYBOARD_DOWN;
				break;
			case 72: // ��
				ch = KEYBOARD_UP;
				break;
			case 77: // ��
				ch = KEYBOARD_RIGHT;
				break;
			case 12: // ^L��ˢ�£�
				ch = KEYBOARD_REFRESH;
				break;
			default:
				break;
			}
		}
		else if (ch == 0) {
#ifdef WIN32
			ch = getch();
#else
			ch = getchar();
#endif
			switch (ch)
			{
			case 59: // F1
			case 60: // F2
			case 61: // F3
			case 62: // F4
			case 63: // F5
			case 64: // F6
			case 65: // F7
			case 66: // F8
			case 67: // F9
			case 68: // F10
				break;
			default:
				break;
			}
		}
		else {
			switch (ch)
			{
			case 12: // ^L��ˢ�£�
				ch = KEYBOARD_REFRESH;
				break;
			default:
				break;
			}
		}
		post_task(std::bind(on_key_pressed, ch));
	}

	return 0;
}

void post_task(std::function<void()> task)
{
	_lock.lock();
	_vTasks.push_back(task);
	_lock.unlock();
	_sem.signal();
}

void HandleTickTimeout()
{
	display_status();
	refresh();
}

int move_forward_1_line()
{
	if(vquotes.size()==0)
		return 0;
	if(curr_line==0){	// first select
		curr_line=1;
		mvchgat(curr_line,0,-1,A_REVERSE,0,NULL);
		return 0;
	}
	if(curr_line==vquotes.size()-curr_pos)	// Already bottom
		return 0;
	if(curr_line!=max_lines){
		mvchgat(curr_line,0,-1,A_NORMAL,0,NULL);
		curr_line++;
		mvchgat(curr_line,0,-1,A_REVERSE,0,NULL);
	}else{
		mvchgat(curr_line,0,-1,A_NORMAL,0,NULL);
		move(1,0);
		setscrreg(1,max_lines);
		scroll(stdscr);
		setscrreg(0,max_lines+1);
		curr_pos++;
		display_quotation(vquotes[curr_pos+max_lines-1].product_id);	// new line
		mvchgat(curr_line,0,-1,A_REVERSE,0,NULL);
	}

	return 0;
}
int scroll_left_1_column()
{
	int i;
	
	if(curr_col_pos==2)
		return 0;
	while(mcolumns[vcolumns[--curr_col_pos]]==false); //	ȡ�������еķ�����ʾ
	display_title();
	if(vquotes.size()==0)
		return 0;
	for(i=curr_pos;i<vquotes.size() && i<curr_pos+max_lines;i++)
		display_quotation(vquotes[i].product_id);
	
	return 0;
}
int scroll_right_1_column()
{
	int i;
	
	if(curr_col_pos==sizeof(column_items)/sizeof(column_item_t)-1)
		return 0;
	while(mcolumns[vcolumns[++curr_col_pos]]==false); //	ȡ�������еķ�����ʾ
	display_title();
	if(vquotes.size()==0)
		return 0;
	for(i=curr_pos;i<vquotes.size() && i<curr_pos+max_lines;i++)
		display_quotation(vquotes[i].product_id);

	return 0;
}

int move_backward_1_line()
{
	if(vquotes.size()==0)
		return 0;
	if(curr_line==0){	// first select
		curr_line=1;
		mvchgat(curr_line,0,-1,A_REVERSE,0,NULL);
		return 0;
	}
	if(curr_line==1 && curr_pos==0)	// Already top
		return 0;
	if(curr_line>1){
		mvchgat(curr_line,0,-1,A_NORMAL,0,NULL);
		curr_line--;
		mvchgat(curr_line,0,-1,A_REVERSE,0,NULL);
	}else{
		mvchgat(curr_line,0,-1,A_NORMAL,0,NULL);
		move(1,0);
		setscrreg(1,max_lines);
		scrl(-1);
		setscrreg(0,max_lines+1);
		curr_pos--;
		display_quotation(vquotes[curr_pos].product_id);
		mvchgat(curr_line,0,-1,A_REVERSE,0,NULL);
	}

	return 0;
}

void focus_quotation(int index)
{
	if(vquotes.size()==0)
		return;
	if(curr_line==0){	// first select
		curr_line=1;
		mvchgat(curr_line,0,-1,A_REVERSE,0,NULL);
	}
	if(index>curr_pos+max_lines-1){
		mvchgat(curr_line,0,-1,A_NORMAL,0,NULL);
		curr_line=max_lines;
		mvchgat(curr_line,0,-1,A_REVERSE,0,NULL);
		int i,n;
		
		n=index-(curr_pos+max_lines-1);
		for(i=0;i<n;i++)
			move_forward_1_line();
	}else if(index<curr_pos){
		mvchgat(curr_line,0,-1,A_NORMAL,0,NULL);
		curr_line=1;
		mvchgat(curr_line,0,-1,A_REVERSE,0,NULL);
		int i,n;
		
		n=curr_pos-index;
		for(i=0;i<n;i++)
			move_backward_1_line();
	}else{
		mvchgat(curr_line,0,-1,A_NORMAL,0,NULL);
		curr_line=index-curr_pos+1;
		mvchgat(curr_line,0,-1,A_REVERSE,0,NULL);
	}
}

void display_quotation(const char *product_id)
{
	int i,y,x,pos,maxy,maxx;
	std::vector<int>::iterator iter;

	getmaxyx(stdscr,maxy,maxx);
	i = mquotes[product_id];
	if(i<curr_pos || i>curr_pos+max_lines-1)
		return;
	y=i-curr_pos+1;
	x=0;

	move(y,0);
	clrtoeol();

	double previous_close = vquotes[i].prev_close;
	if (previous_close == DBL_MAX || fabs(previous_close) < 0.000001)
		previous_close = vquotes[i].prev_settle;

	for(iter=vcolumns.begin(),pos=0;iter!=vcolumns.end();iter++,pos++){
		if(mcolumns[*iter]==false)
			continue;
		if(*iter!=COL_SYMBOL && *iter!=COL_SYMBOL_NAME && pos<curr_col_pos)
			continue;
		if(maxx-x<column_items[*iter].width)
			break;
		switch(*iter){
		case COL_SYMBOL:		//product_id
			mvprintw(y,x,"%-*s",column_items[COL_SYMBOL].width,vquotes[i].product_id);
			x+=column_items[COL_SYMBOL].width;
			break;
		case COL_SYMBOL_NAME:		//product_name
			mvprintw(y,x,"%-*s",column_items[COL_SYMBOL].width,vquotes[i].product_name);
			x+=column_items[COL_SYMBOL_NAME].width+1;
			break;
		case COL_CLOSE:		//close
			if(vquotes[i].price==DBL_MAX)
				mvprintw(y,x,"%*c",column_items[COL_CLOSE].width,'-');
			else
			mvprintw(y,x,"%*.*f",column_items[COL_CLOSE].width,vquotes[i].precision,vquotes[i].price);
			x+=column_items[COL_CLOSE].width+1;
			break;
		case COL_PERCENT:		//close
			if (previous_close == DBL_MAX || fabs(previous_close) < 0.000001 || vquotes[i].price == DBL_MAX || fabs(vquotes[i].price) < 0.000001)
				mvprintw(y, x, "%*c", column_items[COL_PERCENT].width, '-');
			else
				mvprintw(y, x, "%*.1f%%", column_items[COL_PERCENT].width - 1, (vquotes[i].price - previous_close) / previous_close * 100.0);
			x+=column_items[COL_PERCENT].width+1;
			break;
		case COL_ADVANCE:		//close
			if(vquotes[i].prev_settle==DBL_MAX || vquotes[i].prev_settle==0 || vquotes[i].price==DBL_MAX || vquotes[i].price==0)
				mvprintw(y,x,"%*c",column_items[COL_ADVANCE].width,'-');
			else if(vquotes[i].price>vquotes[i].prev_settle)
				mvprintw(y,x,"%*.*f",column_items[COL_ADVANCE].width-1,vquotes[i].precision,vquotes[i].price-vquotes[i].prev_settle);
			else
				mvprintw(y,x,"%*.*f",column_items[COL_ADVANCE].width-1,vquotes[i].precision,vquotes[i].price-vquotes[i].prev_settle);
			x+=column_items[COL_ADVANCE].width+1;
			break;
		case COL_VOLUME:		//volume
			mvprintw(y,x,"%*d",column_items[COL_VOLUME].width,vquotes[i].quantity);
			x+=column_items[COL_VOLUME].width+1;
			break;
		case COL_BID_PRICE:		//close
			if(vquotes[i].buy_price==DBL_MAX)
				mvprintw(y,x,"%*c",column_items[COL_BID_PRICE].width,'-');
			else
				mvprintw(y,x,"%*.*f",column_items[COL_BID_PRICE].width,vquotes[i].precision,vquotes[i].buy_price);
			x+=column_items[COL_BID_PRICE].width+1;
			break;
		case COL_BID_VOLUME:		//volume
			mvprintw(y,x,"%*d",column_items[COL_BID_VOLUME].width,vquotes[i].buy_quantity);
			x+=column_items[COL_BID_VOLUME].width+1;
			break;
		case COL_ASK_PRICE:		//close
			if(vquotes[i].sell_price==DBL_MAX)
				mvprintw(y,x,"%*c",column_items[COL_ASK_PRICE].width,'-');
			else
				mvprintw(y,x,"%*.*f",column_items[COL_ASK_PRICE].width,vquotes[i].precision,vquotes[i].sell_price);
			x+=column_items[COL_ASK_PRICE].width+1;
			break;
		case COL_ASK_VOLUME:		//volume
			mvprintw(y,x,"%*d",column_items[COL_ASK_VOLUME].width,vquotes[i].sell_quantity);
			x+=column_items[COL_ASK_VOLUME].width+1;
			break;
		case COL_OPEN:		//close
			if(vquotes[i].open_price==DBL_MAX)
				mvprintw(y,x,"%*c",column_items[COL_OPEN].width,'-');
			else
				mvprintw(y,x,"%*.*f",column_items[COL_OPEN].width,vquotes[i].precision,vquotes[i].open_price);
			x+=column_items[COL_OPEN].width+1;
			break;
		case COL_PREV_SETTLEMENT:		//close
			if(vquotes[i].prev_settle==DBL_MAX)
				mvprintw(y,x,"%*c",column_items[COL_PREV_SETTLEMENT].width,'-');
			else
				mvprintw(y,x,"%*.*f",column_items[COL_PREV_SETTLEMENT].width,vquotes[i].precision,vquotes[i].prev_settle);
			x+=column_items[COL_PREV_SETTLEMENT].width+1;
			break;
		case COL_TRADE_VOLUME:		//volume
			mvprintw(y,x,"%*d",column_items[COL_TRADE_VOLUME].width,vquotes[i].trade_volume);
			x+=column_items[COL_TRADE_VOLUME].width+1;
			break;
		case COL_AVERAGE_PRICE:		//close
			if(vquotes[i].average_price==DBL_MAX)
				mvprintw(y,x,"%*c",column_items[COL_AVERAGE_PRICE].width,'-');
			else
				mvprintw(y,x,"%*.*f",column_items[COL_AVERAGE_PRICE].width,vquotes[i].precision,vquotes[i].average_price);
			x+=column_items[COL_AVERAGE_PRICE].width+1;
			break;
		case COL_HIGH:		//close
			if(vquotes[i].high==DBL_MAX)
				mvprintw(y,x,"%*c",column_items[COL_HIGH].width,'-');
			else
				mvprintw(y,x,"%*.*f",column_items[COL_HIGH].width,vquotes[i].precision,vquotes[i].high);
			x+=column_items[COL_HIGH].width+1;
			break;
		case COL_LOW:		//close
			if(vquotes[i].low==DBL_MAX)
				mvprintw(y,x,"%*c",column_items[COL_LOW].width,'-');
			else
				mvprintw(y,x,"%*.*f",column_items[COL_LOW].width,vquotes[i].precision,vquotes[i].low);
			x+=column_items[COL_LOW].width+1;
			break;
		case COL_HIGH_LIMIT:		//close
			if (vquotes[i].high_limit == DBL_MAX)
				mvprintw(y, x, "%*c", column_items[COL_HIGH].width, '-');
			else
				mvprintw(y, x, "%*.*f", column_items[COL_HIGH].width, vquotes[i].precision, vquotes[i].high_limit);
			x += column_items[COL_HIGH].width + 1;
			break;
		case COL_LOW_LIMIT:		//close
			if (vquotes[i].low_limit == DBL_MAX)
				mvprintw(y, x, "%*c", column_items[COL_HIGH].width, '-');
			else
				mvprintw(y, x, "%*.*f", column_items[COL_HIGH].width, vquotes[i].precision, vquotes[i].low_limit);
			x += column_items[COL_HIGH].width + 1;
			break;
		case COL_SETTLEMENT:		//close
			if(vquotes[i].settle==DBL_MAX)
				mvprintw(y,x,"%*c",column_items[COL_SETTLEMENT].width,'-');
			else
				mvprintw(y,x,"%*.*f",column_items[COL_SETTLEMENT].width,vquotes[i].precision,vquotes[i].settle);
			x+=column_items[COL_SETTLEMENT].width+1;
			break;
		case COL_PREV_CLOSE:		//close
			if(vquotes[i].prev_close==DBL_MAX)
				mvprintw(y,x,"%*c",column_items[COL_PREV_CLOSE].width,'-');
			else
				mvprintw(y,x,"%*.*f",column_items[COL_PREV_CLOSE].width,vquotes[i].precision,vquotes[i].prev_close);
			x+=column_items[COL_PREV_CLOSE].width+1;
			break;
		case COL_OPENINT:		//volume
			mvprintw(y,x,"%*d",column_items[COL_OPENINT].width,vquotes[i].openint);
			x+=column_items[COL_OPENINT].width+1;
			break;
		case COL_PREV_OPENINT:		//volume
			mvprintw(y,x,"%*d",column_items[COL_PREV_OPENINT].width,vquotes[i].prev_openint);
			x+=column_items[COL_PREV_OPENINT].width+1;
			break;
		case COL_TRADINGDAY:		//volume
			mvprintw(y, x, "%-*s", column_items[COL_TRADINGDAY].width, vquotes[i].tradingday);
			x += column_items[COL_TRADINGDAY].width + 1;
			break;
		default:
			break;
		}
	}

	if(curr_line!=0)
		mvchgat(curr_line,0,-1,A_REVERSE,0,NULL);
}

void display_status()
{
	int y,x;
	char status[100]={0};
	char tradetime[20]={0};

	struct tm *t;
	time_t tt;
	getmaxyx(stdscr,y,x);
	tt=time(NULL);
	t=localtime(&tt);
	sprintf(tradetime,"%02d:%02d:%02d",t->tm_hour,t->tm_min,t->tm_sec);
	switch (ConnectionStatus)
	{
	case CONNECTION_STATUS_DISCONNECTED:
		strcpy(status,"����");
		break;
	case CONNECTION_STATUS_CONNECTED:
		strcpy(status,"����");
		break;
	case CONNECTION_STATUS_LOGINOK:
		strcpy(status,"����");
		break;
	case CONNECTION_STATUS_LOGINFAILED:
		strcpy(status,"����");
		break;
	default:
		strcpy(status,"");
		break;
	}
	move(y-1,0);
	clrtoeol();
	
	mvprintw(y-1,0,"[%d/%d]",curr_pos+curr_line,vquotes.size());
	mvprintw(y-1,x-35,"krenx@qq.com  %s  %s", status,tradetime);
}

void init_screen()
{
	int i,y,x;

	initscr();
	cbreak();
	nodelay(stdscr,TRUE);
	keypad(stdscr,TRUE);
	noecho();
	curs_set(0);
	scrollok(stdscr,TRUE);
	clear();
	getmaxyx(stdscr,y,x);
	max_lines=y-2;
	display_title();
	for(i=0;i<vquotes.size();i++)
		display_quotation(vquotes[i].product_id);
	display_status();
	if(curr_line!=0)
		mvchgat(curr_line,0,-1,A_REVERSE,0,NULL);
}

void refresh_screen()
{
	int i,y,x;

	endwin();
	initscr();
	cbreak();
	nodelay(stdscr,TRUE);
	keypad(stdscr,TRUE);
	noecho();
	curs_set(0);
	scrollok(stdscr,TRUE);
	clear();
	getmaxyx(stdscr,y,x);
	max_lines=y-2;
	display_title();
	for(i=0;i<vquotes.size();i++)
		display_quotation(vquotes[i].product_id);
	display_status();
	if(curr_line!=0)
		mvchgat(curr_line,0,-1,A_REVERSE,0,NULL);
}


void display_title()
{
	int y,x,pos,maxy,maxx;
	std::vector<int>::iterator iter;

	getmaxyx(stdscr,maxy,maxx);
	y=0;
	x=0;
	move(0,0);
	clrtoeol();
	for(iter=vcolumns.begin(),pos=0;iter!=vcolumns.end();iter++,pos++){
		if(mcolumns[*iter]==false)
			continue;
		if(*iter!=COL_SYMBOL && *iter!=COL_SYMBOL_NAME && pos<curr_col_pos)
			continue;
		if(maxx-x<column_items[*iter].width)
			break;
		switch(*iter){
		case COL_SYMBOL:		//product_id
			mvprintw(y,x,"%-*s",column_items[COL_SYMBOL].width,column_items[COL_SYMBOL].name);
			x+=column_items[COL_SYMBOL].width;
			break;
		case COL_SYMBOL_NAME:		//product_name
			mvprintw(y,x,"%-*s",column_items[COL_SYMBOL_NAME].width,column_items[COL_SYMBOL_NAME].name);
			x+=column_items[COL_SYMBOL_NAME].width+1;
			break;
		case COL_CLOSE:		//close
			mvprintw(y,x,"%*s",column_items[COL_CLOSE].width,column_items[COL_CLOSE].name);
			x+=column_items[COL_CLOSE].width+1;
			break;
		case COL_PERCENT:		//close
			mvprintw(y,x,"%*s",column_items[COL_PERCENT].width,column_items[COL_PERCENT].name);
			x+=column_items[COL_PERCENT].width+1;
			break;
		case COL_ADVANCE:		//close
			mvprintw(y,x,"%*s",column_items[COL_ADVANCE].width,column_items[COL_ADVANCE].name);
			x+=column_items[COL_ADVANCE].width+1;
			break;
		case COL_VOLUME:		//volume
			mvprintw(y,x,"%*s",column_items[COL_VOLUME].width,column_items[COL_VOLUME].name);
			x+=column_items[COL_VOLUME].width+1;
			break;
		case COL_BID_PRICE:		//close
			mvprintw(y,x,"%*s",column_items[COL_BID_PRICE].width,column_items[COL_BID_PRICE].name);
			x+=column_items[COL_BID_PRICE].width+1;
			break;
		case COL_BID_VOLUME:		//volume
			mvprintw(y,x,"%*s",column_items[COL_BID_VOLUME].width,column_items[COL_BID_VOLUME].name);
			x+=column_items[COL_BID_VOLUME].width+1;
			break;
		case COL_ASK_PRICE:		//close
			mvprintw(y,x,"%*s",column_items[COL_ASK_PRICE].width,column_items[COL_ASK_PRICE].name);
			x+=column_items[COL_ASK_PRICE].width+1;
			break;
		case COL_ASK_VOLUME:		//volume
			mvprintw(y,x,"%*s",column_items[COL_ASK_VOLUME].width,column_items[COL_ASK_VOLUME].name);
			x+=column_items[COL_ASK_VOLUME].width+1;
			break;
		case COL_OPEN:		//close
			mvprintw(y,x,"%*s",column_items[COL_OPEN].width,column_items[COL_OPEN].name);
			x+=column_items[COL_OPEN].width+1;
			break;
		case COL_PREV_SETTLEMENT:		//close
			mvprintw(y,x,"%*s",column_items[COL_PREV_SETTLEMENT].width,column_items[COL_PREV_SETTLEMENT].name);
			x+=column_items[COL_PREV_SETTLEMENT].width+1;
			break;
		case COL_TRADE_VOLUME:		//volume
			mvprintw(y,x,"%*s",column_items[COL_TRADE_VOLUME].width,column_items[COL_TRADE_VOLUME].name);
			x+=column_items[COL_TRADE_VOLUME].width+1;
			break;
		case COL_AVERAGE_PRICE:		//close
			mvprintw(y,x,"%*s",column_items[COL_AVERAGE_PRICE].width,column_items[COL_AVERAGE_PRICE].name);
			x+=column_items[COL_AVERAGE_PRICE].width+1;
			break;
		case COL_HIGH:		//close
			mvprintw(y,x,"%*s",column_items[COL_HIGH].width,column_items[COL_HIGH].name);
			x+=column_items[COL_HIGH].width+1;
			break;
		case COL_LOW:		//close
			mvprintw(y,x,"%*s",column_items[COL_LOW].width,column_items[COL_LOW].name);
			x+=column_items[COL_LOW].width+1;
			break;
		case COL_HIGH_LIMIT:		//close
			mvprintw(y, x, "%*s", column_items[COL_HIGH_LIMIT].width, column_items[COL_HIGH_LIMIT].name);
			x += column_items[COL_HIGH_LIMIT].width + 1;
			break;
		case COL_LOW_LIMIT:		//close
			mvprintw(y, x, "%*s", column_items[COL_LOW_LIMIT].width, column_items[COL_LOW_LIMIT].name);
			x += column_items[COL_LOW_LIMIT].width + 1;
			break;
		case COL_SETTLEMENT:		//close
			mvprintw(y,x,"%*s",column_items[COL_SETTLEMENT].width,column_items[COL_SETTLEMENT].name);
			x+=column_items[COL_SETTLEMENT].width+1;
			break;
		case COL_PREV_CLOSE:		//close
			mvprintw(y,x,"%*s",column_items[COL_PREV_CLOSE].width,column_items[COL_PREV_CLOSE].name);
			x+=column_items[COL_PREV_CLOSE].width+1;
			break;
		case COL_OPENINT:		//volume
			mvprintw(y,x,"%*s",column_items[COL_OPENINT].width,column_items[COL_OPENINT].name);
			x+=column_items[COL_OPENINT].width+1;
			break;
		case COL_PREV_OPENINT:		//volume
			mvprintw(y,x,"%*s",column_items[COL_PREV_OPENINT].width,column_items[COL_PREV_OPENINT].name);
			x+=column_items[COL_PREV_OPENINT].width+1;
			break;
		case COL_TRADINGDAY:		//volume
			mvprintw(y, x, "%-*s", column_items[COL_TRADINGDAY].width, column_items[COL_TRADINGDAY].name);
			x += column_items[COL_TRADINGDAY].width + 1;
			break;
		default:
			break;
		}
	}
}

void work_thread()
{
	// Init screen
	init_screen();
	
	// Run
	while (true) {
		_sem.wait();
		_lock.lock();
		auto task = _vTasks.begin();
		if (task == _vTasks.end()) {
			_lock.unlock();
			continue;
		}
		(*task)();
		_vTasks.erase(task);
		_lock.unlock();
	}

	// End screen
	endwin();
}

void time_thread()
{
	while (true) {
		std::this_thread::sleep_for(std::chrono::seconds(1));
		post_task(std::bind(HandleTickTimeout));
	}
}

int on_key_pressed(int ch)
{
	int r=0;
	r=on_key_pressed_mainboard(ch);

	refresh();

	if(r<0){
		endwin();
		exit(0);
	}
	
	return 0;
}


int on_key_pressed_mainboard(int ch)
{
	if(ch =='q'){
		return -1;
	}

	switch(ch){
	case KEYBOARD_REFRESH:		// ^L
		refresh_screen();
		break;
	case 'k':
	case KEYBOARD_UP:
		move_backward_1_line();
		break;
	case 'j':
	case KEYBOARD_DOWN:
		move_forward_1_line();
		break;
	case 'h':
	case KEYBOARD_LEFT:
		scroll_left_1_column();
		break;
	case 'l':
	case KEYBOARD_RIGHT:
		scroll_right_1_column();
		break;
	default:
		break;
	}
	display_status();

	return 0;
}
