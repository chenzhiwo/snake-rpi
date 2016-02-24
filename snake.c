#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include "fcntl.h"
#include "termios.h"
#include <wiringPi.h>
#include <libssd1306.h>
#include <libtftgfx.h>

//定义了贪食蛇盘的边界
#define BORDER_X	0
#define BORDER_Y	0
#define BORDER_W	41
#define BORDER_H	20
//画面元素的像素宽度
#define PIXEL		3

//蛇头的初始位置
#define INIT_POS_X	5
#define INIT_POS_Y	5
//初始长度
#define INIT_LENGTH	5
//游戏的速度
#define DELAY_TIME 100
//终端参数
#define ECHOFLAGS (ECHO|ECHOE|ECHOK|ECHONL|ICANON)

//取得指定范围内的一个随机数，min<=rand<max
#define RAND_NUM(min, max) ( (min) + (rand() % ( (max) - (min) )))

//记录方向的枚举
enum DIRECTION {
	UP = 1, DOWN = -1, NONE = 0, LEFT = 2, RIGHT = -2
};

//一个点结构体，所有的操作都是基于点
typedef struct dot_str {
	int x;
	int y;
} dot_str;

//蛇链表
typedef struct snake_str {
	struct dot_str pos;
	struct snake_str *next;
} snake_str;

//比较复杂的调试信息可以sprintf先放到这里
char logbuf[1024] = "";
/*-----------------------------------------------------------------------------
 *  统一的输出调试信息的函数
 *-----------------------------------------------------------------------------*/
void logger(char msg[])
{
	fprintf(stderr, "%d-LOG:%s\n", getpid(), msg);
}
/*-----------------------------------------------------------------------------
 *  判断pdot是否在以dot1和dot2为端点的线段中
 *-----------------------------------------------------------------------------*/
int is_dot_in_line(dot_str pdot, dot_str dot1, dot_str dot2)
{
	if(dot1.x == dot2.x && pdot.x == dot1.x)
	{
		//垂直线
		if( (dot1.y <= pdot.y && pdot.y <= dot2.y) ||
				(dot2.y <= pdot.y && pdot.y <= dot1.y) )
		{
			return 1;
		}

	}

	if(dot1.y == dot2.y && pdot.y == dot1.y)
	{
		//水平线
		if( (dot1.x <= pdot.x && pdot.x <= dot2.x) ||
				(dot2.x <= pdot.x && pdot.x <= dot1.x) )
		{
			return 1;
		}
	}

	return 0;
}

/*-----------------------------------------------------------------------------
 *  判断一点dot是否在snake中
 *-----------------------------------------------------------------------------*/
int is_dot_in_snake(snake_str *snake, dot_str dot)
{
	while(snake->next != NULL)
	{
		if(is_dot_in_line(dot, snake->pos, snake->next->pos) == 1)
		{
			return 1;
		}
		snake = snake->next;
	}
	return 0;
}

/*-----------------------------------------------------------------------------
 *  判断蛇头是否撞到了自己
 *-----------------------------------------------------------------------------*/
int is_snake_crash_self(snake_str *snake)
{
	int node_num = 0;
	dot_str dot = {};
	/*-----------------------------------------------------------------------------
	 *  首先要判断蛇的节点数，蛇没有四节，是碰不到自己的啦，同样的，判断的时候只需要
	 *  判断蛇头是否在第三节以后的身体以内就可以了
	 *-----------------------------------------------------------------------------*/
	//首先要记录好蛇头的位置
	dot = snake->pos;
	while(snake->next != NULL)
	{
		node_num++;
		//节点大于3才需要判断是否会碰到自己
		if( node_num > 3 )
		{
			if(is_dot_in_line(dot, snake->pos, snake->next->pos) == 1)
			{
				return 1;
			}
		}
		snake = snake->next;
	}

	return 0;
}

/*-----------------------------------------------------------------------------
 *  判断蛇是否碰到边界或者自己
 *-----------------------------------------------------------------------------*/
int is_snake_crash(snake_str *snake)
{
	//四个边界的点坐标
	dot_str dot1, dot2, dot3, dot4;
	//是否碰到自己
	if(is_snake_crash_self(snake) == 1)
	{
		return 1;
	}
	dot1.x = BORDER_X ;
	dot1.y = BORDER_Y ;
	dot2.x = BORDER_X + BORDER_W;
	dot2.y = BORDER_Y ;
	dot3.x = BORDER_X ;
	dot3.y = BORDER_Y + BORDER_H ;
	dot4.x = BORDER_X + BORDER_W ;
	dot4.y = BORDER_Y + BORDER_H ;

	//是否碰到边界
	if( (is_dot_in_line(snake->pos, dot1, dot2) == 1) ||
			(is_dot_in_line(snake->pos, dot1, dot3) == 1) ||
			(is_dot_in_line(snake->pos, dot2, dot4) == 1) ||
			(is_dot_in_line(snake->pos, dot3, dot4) == 1) )
	{
		return 1;
	}

	return 0;
}

/*-----------------------------------------------------------------------------
 *  判断食物是否已经被吃掉，是则随机生成一个新的食物到food
 *-----------------------------------------------------------------------------*/
int is_snake_eaten(snake_str *snake, dot_str *food)
{
	if(snake->pos.x == food->x && snake->pos.y == food->y)
	{
		//随机生成一个不在蛇体内的食物
		do
		{
			food->x = RAND_NUM( BORDER_X + 1, BORDER_X + BORDER_W);
			food->y = RAND_NUM( BORDER_Y + 1, BORDER_Y + BORDER_H);
		}while( is_dot_in_snake(snake, *food) == 1);
		sprintf(logbuf, "food:%d,%d", food->x, food->y);
		logger(logbuf);
		return 1;
	}
	return 0;
}

/*-----------------------------------------------------------------------------
 *  把终端设置为无回显模式，便于接收按键事件，并且把原终端配置保存于term_attr
 *  中，退出的时候要使用key_restore()来恢复终端配置
 *-----------------------------------------------------------------------------*/
void key_init(struct termios *term_attr)
{
	struct termios new_attr;
	int flag = 0;
	if(tcgetattr(STDIN_FILENO,term_attr) == -1)
	{
		exit(1);
	}
	new_attr = *term_attr;
	//取消回显
	new_attr.c_lflag = new_attr.c_lflag & (~ECHOFLAGS);

	if(tcsetattr(STDIN_FILENO,TCSAFLUSH, &new_attr) == -1)
	{
		tcsetattr(STDIN_FILENO, TCSAFLUSH, term_attr);
		exit(1);
	}

	//设置为非阻塞，这样获取按键就不需要按回车了
	flag = fcntl(STDIN_FILENO, F_GETFL);
	flag = flag | O_NONBLOCK;
	if(fcntl(STDIN_FILENO, F_SETFL,flag) == -1)
	{
		exit(1);
	}
}

/*-----------------------------------------------------------------------------
 *  根据term_attr来恢复终端设置
 *-----------------------------------------------------------------------------*/
void key_restore(struct termios *term_attr)
{
	int flag = 0;
	tcsetattr(STDIN_FILENO,TCSAFLUSH,term_attr);
	flag = fcntl(STDIN_FILENO, F_GETFL);
	flag = flag | O_NONBLOCK;
	fcntl(STDIN_FILENO, F_SETFL,flag);
}

enum DIRECTION get_dire_keyboard(char key)
{
	switch(key)
	{
		case 'w':
			return UP;
			break;
		case 's':
			return DOWN;
			break;
		case 'a':
			return LEFT;
			break;
		case 'd':
			return RIGHT;
			break;
		default:
			break;
	}
	return NONE;
}

void wait_for_key(char key)
{
	while(getchar() != key);
}

/*-----------------------------------------------------------------------------
 *	返回dot2指向dot1的方向
 *-----------------------------------------------------------------------------*/
enum DIRECTION direction(dot_str dot1, dot_str dot2)
{
	//如果两点的坐标相同的话，那么就是同一点咯，所以是没有方向的
	if(dot1.x == dot2.x && dot1.y == dot2.y)
	{
		return NONE;
	}
	//垂直线
	if(dot1.x == dot2.x)
	{
		if(dot1.y > dot2.y)
		{
			return DOWN;
		}
		else
		{
			return UP;
		}

	}

	//水平线
	if(dot1.y == dot1.y)
	{
		if(dot1.x > dot2.x)
		{
			return RIGHT;
		}
		else
		{
			return LEFT;
		}

	}
	return NONE;
}

/*-----------------------------------------------------------------------------
 *  创建一条snake链表
 *-----------------------------------------------------------------------------*/
snake_str * snake_create(void)
{
	snake_str *snake = NULL;
	//为头节点分配内存，设置初始坐标后面的节点就像链表一样延伸下去
	snake = (snake_str *) malloc(sizeof(snake_str));
	snake->pos.x = INIT_POS_X;
	snake->pos.y = INIT_POS_Y;
	//一开始蛇是直的，所以只需要两个节点，为尾节点分配内存，设置初始坐标
	snake->next = (snake_str *) malloc(sizeof(snake_str));
	snake->next->pos.x = INIT_POS_X - INIT_LENGTH + 1;
	snake->next->pos.y = INIT_POS_Y;
	snake->next->next = NULL;
	return snake;
}

/*-----------------------------------------------------------------------------
 *  释放snake链表的内存
 *-----------------------------------------------------------------------------*/
void snake_killall(snake_str *snake)
{
	snake_str *tmp_snake = NULL;
	while(snake != NULL)
	{
		/*-----------------------------------------------------------------------------
		 *  需要有一个临时变量来保存下一节点的地址，否则释放了当前节点以后就再也找不到
		 *  下一个节点了
		 *-----------------------------------------------------------------------------*/
		tmp_snake = snake->next;
		free(snake);
		snake = tmp_snake;
	}
}

/*-----------------------------------------------------------------------------
 *  snake前进一步，如果dire和原来一样那么就是直走，如果eaten == 1那么就相当于
 *  蛇吃到了食物，所以在蛇头前进一个单位的同时保持蛇尾不移动，就达到了蛇变长的
 *  效果了
 *-----------------------------------------------------------------------------*/
void snake_move(snake_str *snake, enum DIRECTION new_dire, int *eaten)
{
	//目前的方向
	snake_str *tmp_snake = NULL;
	enum DIRECTION cur_dire = NONE;
	cur_dire = direction(snake->pos, snake->next->pos);

	//必须保证新的方向和原来不相反
	if( cur_dire == -new_dire || cur_dire == NONE || cur_dire == new_dire)
	{
		new_dire = cur_dire;
	}

	//如果要转向，那么链表就要增加新的项
	if( cur_dire != new_dire && new_dire != NONE)
	{
		/*-----------------------------------------------------------------------------
		 *  如果要改变方向，就要以新的snake来取代第一个snake的位置，然后再对第一个snake
		 *  进行移动
		 *-----------------------------------------------------------------------------*/
		cur_dire = new_dire;
		tmp_snake = (snake_str *) malloc(sizeof(snake_str));
		*tmp_snake = *snake;
		snake->next = tmp_snake;
	}

	//移动蛇头
	switch(cur_dire)
	{
		case UP:
			snake->pos.y--;
			break;
		case DOWN:
			snake->pos.y++;
			break;
		case LEFT:
			snake->pos.x--;
			break;
		case RIGHT:
			snake->pos.x++;
			break;
		case NONE:
			break;
		default:
			break;
	}

	//如果蛇没有吃到食物，那么蛇尾就要移动
	if(*eaten != 1)
	{
		//找出倒数第二个node
		while(snake->next->next != NULL)
		{
			snake = snake->next;
		}
		cur_dire = direction(snake->pos, snake->next->pos);
		switch(cur_dire)
		{
			case UP:
				snake->next->pos.y--;
				break;
			case DOWN:
				snake->next->pos.y++;
				break;
			case LEFT:
				snake->next->pos.x--;
				break;
			case RIGHT:
				snake->next->pos.x++;
				break;
			case NONE:
				break;
			default:
				break;
		}
		//如果移动以后，最后两node重合，那么释放最后一个node
		if( snake->pos.x == snake->next->pos.x &&
				snake->pos.y == snake->next->pos.y )
		{
			free(snake->next);
			snake->next = NULL;
		}

	}
	*eaten = 0;
}

/*-----------------------------------------------------------------------------
 *  画一个方块
 *-----------------------------------------------------------------------------*/
void draw_dot(dot_str dot)
{
	int i = 0;
	for(i = 0; i < PIXEL; i++)
	{
		gfxDrawLine(dot.x * PIXEL, dot.y * PIXEL + i,
				dot.x * PIXEL + PIXEL - 1, dot.y * PIXEL+ i, COLOR_ON);
	}
}

/*-----------------------------------------------------------------------------
 *  把游戏中方格的相对坐标的线变成屏幕上的真正坐标，游戏中的坐标代表一个方格的坐标
 *  一个方格的长宽为PIXEL宏的大小
 *-----------------------------------------------------------------------------*/
void draw_line(dot_str dot1, dot_str dot2)
{
	//实际上只是根据线的坐标画出一个实心矩形
	int i = 0;
	if( dot1.y == dot2.y )
	{
		//水平线
		for( i = 0; i < PIXEL; i++)
		{
			gfxDrawLine( dot1.x * PIXEL, (dot1.y * PIXEL) + i,
					dot2.x * PIXEL, (dot2.y * PIXEL) + i, COLOR_ON);
		}
	}
	else if( dot1.x == dot2.x )
	{
		//垂直线
		for( i = 0; i < PIXEL; i++)
		{
			gfxDrawLine( (dot1.x * PIXEL) + i, dot1.y * PIXEL,
					(dot2.x * PIXEL) + i, dot2.y * PIXEL, COLOR_ON);
		}
	}
	//描一下转角位置，避免出现缺角
	draw_dot(dot1);
	draw_dot(dot2);
}

/*-----------------------------------------------------------------------------
 * 绘制边界
 *-----------------------------------------------------------------------------*/
void draw_border(void)
{
	dot_str dot1, dot2, dot3, dot4;

	dot1.x = BORDER_X ;
	dot1.y = BORDER_Y ;
	dot2.x = BORDER_X + BORDER_W;
	dot2.y = BORDER_Y ;
	dot3.x = BORDER_X ;
	dot3.y = BORDER_Y + BORDER_H ;
	dot4.x = BORDER_X + BORDER_W ;
	dot4.y = BORDER_Y + BORDER_H ;

	draw_line(dot1, dot2);
	draw_line(dot1, dot3);
	draw_line(dot2, dot4);
	draw_line(dot3, dot4);

}

/*-----------------------------------------------------------------------------
 *  把一个蛇链表对应的图形写到buffer中，不改变链表内容，不刷新数据到显存
 *-----------------------------------------------------------------------------*/
void draw_snake(snake_str *snake)
{
	while(snake->next != NULL)
	{
		draw_line(snake->pos, snake->next->pos);
		snake = snake->next;
	}

}

int main(int argc, char * argv[])
{

	snake_str *snake = NULL;
	dot_str food = {};
	struct termios term_attr = {};
	int eaten = 0;
	enum DIRECTION dire = NONE;
	int score = 0;

	ssd_init(22, 23, 24, 25);
	ssd_clean();
	/* -----------------------------------------------------------------------------
	 *          *  把ssd1306库包装好的底层函数作为参数来初始化libtftgfx库，初始化以后就可以使用
	 *                   *  libtftgfx库提供的一系列函数来绘图了
	 *                            *-----------------------------------------------------------------------------*/
	gfxInitStruct gfx = {};
	gfx.HWFlushFunc = ssd_buffer_flush;
	gfx.HWClearFunc = ssd_buffer_clear;
	gfx.HWDrawPixelFunc = ssd_buffer_draw_pixel;
	gfx.HWDrawBitMapFunc = ssd_buffer_draw_bitmap;
	gfx.HWDrawBitMapBinFunc = ssd_buffer_draw_bitmapbin;
	gfxInit(gfx);

	srand((unsigned)time(NULL));
	snake = snake_create();
	food.x = INIT_POS_X + 10;
	food.y = INIT_POS_Y;
	is_snake_eaten(snake, &food);
	key_init(&term_attr);

	while(1)
	{
		gfxClear();
		draw_border();
		draw_dot(food);
		draw_snake(snake);
		gfxFlush();

		if(is_snake_crash(snake) == 1)
		{
			logger("game over.");
			gfxClear();
			gfxDrawFillRect(BORDER_X , BORDER_Y, BORDER_W * PIXEL, BORDER_H * PIXEL, COLOR_ON);
			sprintf(logbuf, "SCORE:%d", score);
			gfxPrintASCII5x8(8,8, "GAME OVER!", COLOR_NONE, COLOR_DEFAULT);
			gfxPrintASCII5x8(8,16, logbuf, COLOR_NONE, COLOR_DEFAULT);
			gfxFlush();
			snake_killall(snake);
			key_restore(&term_attr);
			exit(0);
		}
		if(is_snake_eaten(snake, &food) == 1)
		{
			eaten = 1;
			score += (DELAY_TIME / 10);
		}
		dire = get_dire_keyboard(getchar());

		snake_move(snake, dire, &eaten);
		delay(DELAY_TIME);
	}

	snake_killall(snake);
	key_restore(&term_attr);
	return EXIT_SUCCESS;
}
