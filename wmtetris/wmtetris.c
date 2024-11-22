#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <X11/Xlib.h>
#include <X11/xpm.h>

#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#include <fcntl.h>
#include <linux/joystick.h>

#define MAX_LINES 1000000  // 最大行数の制限

#include "../wmgeneral/wmgeneral.h"
//#include "wmtetris.h"

#include "wmtetris.xpm"
#include "wmtetris-mask.xbm"

#define TEMPORAL_RESOLUTION 100000	// 100000 = 0.1 sec
#define INITIAL_DELAY      1000000
#define FAST_MODE_DELAY     100000
#define DELAY_INCREMENT      10000

#define BLOCK_SIZE 3
#define BOARD_POS_X 4
#define BOARD_POS_Y 6
#define BOARD_WIDTH 10
#define BOARD_HEIGHT 18
#define NEXT_POS_X 46
#define NEXT_POS_Y 7

#define BUTTONC 6
#define BUTTON_NONE 0
#define BUTTON_ROTATE_LEFT 1
#define BUTTON_ROTATE_RIGHT 2
#define BUTTON_MOVE_LEFT 3
#define BUTTON_MOVE_RIGHT 4
#define BUTTON_MOVE_DOWN 5

#define JOYSTICK_DEV "/dev/input/js0"
int js_fd = -1; // ジョイスティックファイルディスクリプタ
char filepath[256];
int input_index = 0;
int max_index = 0;
int numbers[MAX_LINES];  // 数値を格納する配列
int replay_mode = 0;

static int buttons[6][4] = {
	{ 0,  0, 64, 64},
	{43, 31, 51, 40},
	{52, 31, 60, 40},
	{43, 41, 51, 50},
	{52, 41, 60, 50},
	{43, 51, 60, 60}
};

static int initial_figures[7][4][2] = {
	{ {0, 1}, {1, 1}, {2, 1}, {3, 1} },
	{ {0, 1}, {1, 1}, {2, 1}, {2, 2} },
	{ {0, 2}, {1, 2}, {2, 2}, {2, 1} },
	{ {0, 2}, {1, 1}, {1, 2}, {2, 2} },
	{ {0, 2}, {1, 2}, {1, 1}, {2, 1} },
	{ {0, 1}, {1, 1}, {1, 2}, {2, 2} },
	{ {1, 1}, {2, 1}, {1, 2}, {2, 2} }
};

int which_button(int x, int y);
int rotate_figure(int direction, int figure[4][2], int fig_x, int fig_y);
void draw_figure(int figure[4][2], int type, int x, int y);
void draw_next_figure(int figure[4][2], int type);
void general_draw_figure(int base_x, int base_y, int figure[4][2],
						 int type, int x, int y);
void full_refresh();
int check_figure_position(int fig_x, int fig_y, int figure[4][2]);
void append_to_file(const char *filepath, const char *content);

// グローバル変数として display を定義
Display *display = NULL;

void setupJoystick() {
    js_fd = open(JOYSTICK_DEV, O_RDONLY | O_NONBLOCK);
    if (js_fd == -1) {
        perror("Failed to open joystick device");
    } else {
        printf("Joystick connected: %s\n", JOYSTICK_DEV);
    }
}

void processJoystickInput(int *fig_x, int *fig_y, int figure[4][2], int figure_type, int *fast_mode) {
    struct js_event js;
	char str[20];     // 結果を格納する文字列バッファ
	int pad_mask = 0;

	*fast_mode = 0;
    while (read(js_fd, &js, sizeof(struct js_event)) == sizeof(struct js_event)) {
        if (js.type & JS_EVENT_BUTTON) {
            // ボタンイベント処理
            if (js.value) { // ボタンが押された場合
				// 未入力のときのみ
				if (!(pad_mask & 3))
					switch (js.number) {
						case 0: // ボタン0: 左回転
							rotate_figure(0, figure, *fig_x, *fig_y);
							pad_mask = pad_mask | 1;
							break;
						case 1: // ボタン1: 右回転
							rotate_figure(1, figure, *fig_x, *fig_y);
							pad_mask = pad_mask | 2;
							break;
						case 2: // ボタン2: 高速モード
							//*fast_mode = 1;
							break;
						case 3: // ボタン3: リセット高速モード
							//*fast_mode = 0;
							break;
                }
            }
        } else if (js.type & JS_EVENT_AXIS) {
			// 未入力のときのみ
			if (!(pad_mask & 28))
				// 軸イベント処理
				if (js.number == 0) { // 水平方向（軸0）
					if (js.value < -10000) { // 左
						if (check_figure_position(*fig_x - 1, *fig_y, figure)) {
							(*fig_x)--;
							pad_mask = pad_mask | 4;
						}
					} else if (js.value > 10000) { // 右
						if (check_figure_position(*fig_x + 1, *fig_y, figure)) {
							(*fig_x)++;
							pad_mask = pad_mask | 8;
						}
					}
				} else if (js.number == 1) { // 垂直方向（軸1）

					if (js.value > 10000) { // 下
						*fast_mode = 1;
						pad_mask = pad_mask | 16;
					}
				}
        }
    }
    sprintf(str, "%d", pad_mask); append_to_file(filepath, str);
}
int get_number() {
	if (input_index > max_index) {
		//最後まで読み込み（何もしない）
		return 0;
	};
	int inp = numbers[input_index];
	input_index++;

	return inp;
}
void processJoystickInputFromFile(int *fig_x, int *fig_y, int figure[4][2], int figure_type, int *fast_mode) {

	//Next Input
	int inp = get_number();
	*fast_mode = 0;

	// ボタンイベント処理
	if (inp & 1) {
				rotate_figure(0, figure, *fig_x, *fig_y);
	}
	if (inp & 2) {
				rotate_figure(1, figure, *fig_x, *fig_y);
	}
	if (inp & 4) {
			if (check_figure_position(*fig_x - 1, *fig_y, figure)) {
				(*fig_x)--;
			}
	}
	if (inp & 8) {
			if (check_figure_position(*fig_x + 1, *fig_y, figure)) {
				(*fig_x)++;
			}
	}
	if (inp & 16) {
			*fast_mode = 1;
	}
}

void create_directory_if_not_exists(const char *path) {
	struct stat st = {0};
	// フォルダの存在を確認
	if (stat(path, &st) == -1) {
		// フォルダが存在しない場合、作成
		if (mkdir(path, 0700) == 0) {
			printf("Directory '%s' created successfully.\n", path);
		} else {
			perror("Failed to create directory");
		}
	} else {
		printf("Directory '%s' already exists.\n", path);
	}
}

void append_to_file(const char *filepath, const char *content) {
	FILE *file = fopen(filepath, "a"); // 追記モード ("a")
	if (file == NULL) {
		perror("Failed to open file");
		return;
	}
	fprintf(file, "%s\n", content); // 追記する内容
	fclose(file);
	printf("Appended to file '%s': %s\n", filepath, content);
}
void create_and_append_to_file(const char *filepath, const char *content) {
	FILE *file = fopen(filepath, "w"); // 追記モード ("w")
	if (file == NULL) {
		perror("Failed to open file");
		return;
	}
	fprintf(file, "%s\n", content); // 追記する内容
	fclose(file);
	printf("Appended to file '%s': %s\n", filepath, content);
}


unsigned char board[BOARD_WIDTH][BOARD_HEIGHT];
int score=0;


int main(int argc, char *argv[]) {
	int i, x, y, step, input, fast_mode, progress,
		fig_x, fig_y, new_figure=1, figure_type, next_figure_type;
	int figure[4][2] = { {0, 0}, {0, 0}, {0, 0}, {0, 0} },
		next_figure[4][2] = { {0, 0}, {0, 0}, {0, 0}, {0, 0} };
	unsigned long delay = INITIAL_DELAY, start_time;
	XEvent event;
	char str[20];     // 結果を格納する文字列バッファ

	srand(time(NULL));

	const char *directory = ".wmtetris";
	const char *filename = "replay.log";
    const char *home = getenv("HOME");  // 環境変数 "HOME" を取得
    if (home == NULL) {
        fprintf(stderr, "Failed to get HOME environment variable.\n");
        return 1;
    }
	// フォルダを確認して必要なら作成
	create_directory_if_not_exists(directory);
	// フォルダ内のファイルに追記

	snprintf(filepath, sizeof(filepath), "%s/%s/%s", home, directory, filename);

	// 既存の初期化処理
	setupJoystick(); // ジョイスティック初期化
	// リプレイ読み込み
	if (js_fd == -1) {
		FILE *file = fopen(filepath, "r");    // ファイルを読み込みモードで開く
		if (file == NULL) {
			perror("Failed to open file");
		} else {
			int count = 0;           // 配列の有効要素数

			while (fscanf(file, "%d", &numbers[count]) == 1) {
				count++;
				if (count >= MAX_LINES) {
					printf("Reached maximum line limit (%d).\n", MAX_LINES);
					break;
				}
			}
			fclose(file);  // ファイルを閉じる
			max_index = count;
			if (count > 300) replay_mode = 1;
		}
	}

	// New Game
	for (y = 0; y < BOARD_HEIGHT; y++)
		for (x = 0; x < BOARD_WIDTH; x++)
			board[x][y] = 0;

	openXwindow(argc, argv, wmtetris_xpm, wmtetris_mask_bits,
				wmtetris_mask_width, wmtetris_mask_height);

	NewGame_loop:
	for (y = 0; y < BOARD_HEIGHT; y++)
		for (x = 0; x < BOARD_WIDTH; x++)
			board[x][y] = 0;
	copyXPMArea(64, 0, 64, 64, 0, 0);
	RedrawWindow();

	fast_mode=0;
	if (replay_mode == 0) {
		figure_type = random() % 7;
		sprintf(str, "%d", figure_type); create_and_append_to_file(filepath, str);
	} else {
		figure_type = get_number() & 7;
	}
	for (i = 0; i < 4; i++) {
		figure[i][0] = initial_figures[figure_type][i][0];
		figure[i][1] = initial_figures[figure_type][i][1];
	}
	if (replay_mode == 0) {
		next_figure_type = random() % 7;
		sprintf(str, "%d", next_figure_type); append_to_file(filepath, str);
	} else {
		next_figure_type = get_number() & 7;
	}
	for (i = 0; i < 4; i++) {
		next_figure[i][0] = initial_figures[next_figure_type][i][0];
		next_figure[i][1] = initial_figures[next_figure_type][i][1];
	}
	draw_next_figure(next_figure, next_figure_type);
	fig_x = BOARD_WIDTH / 2 - 2;
	fig_y = 0;
	draw_figure(figure, figure_type, fig_x, fig_y);
	RedrawWindow();

	// Main Loop
	while (1) {
		if (check_figure_position(fig_x, fig_y + 1, figure)) {
			new_figure = 0;
			draw_figure(figure, -1, fig_x, fig_y);
			fig_y++;
		} else {
			new_figure = 1;
			fast_mode = 0;
			for (i = 0; i < 4; i++) {
				board [fig_x + figure[i][0]] [fig_y + figure[i][1]] = figure_type + 1;
			}

			progress=0;
			for (y = 0; y < BOARD_HEIGHT; y++) {
				for (x = 0; x < BOARD_WIDTH; x++)
					if (!board[x][y])
						break;
				if (x == BOARD_WIDTH) {
					for (i = y; i > 0; i--)
						for (x = 0; x < BOARD_WIDTH; x++)
							board[x][i] = board[x][i-1];
					progress++;
				}
			}
			score += progress*progress;

			full_refresh();

			i = score;
			for (x = 3; x >= 0; x--) {
				copyXPMArea(4 * (i % 10), 100, 3, 5, 44 + 4*x, 24);
				i /= 10;
			}

			figure_type = next_figure_type;
			if (replay_mode == 0) {
				next_figure_type = random() % 7;
				sprintf(str, "%d", next_figure_type); append_to_file(filepath, str);
			} else {
				next_figure_type = get_number() & 7;
			}
			for (i = 0; i < 4; i++) {
				figure[i][0] = next_figure[i][0];
				figure[i][1] = next_figure[i][1];
				next_figure[i][0] = initial_figures[next_figure_type][i][0];
				next_figure[i][1] = initial_figures[next_figure_type][i][1];
			}
			draw_next_figure(next_figure, next_figure_type);
			fig_x = BOARD_WIDTH / 2 - 2;
			fig_y = 0;
		}
		draw_figure(figure, figure_type, fig_x, fig_y);
		RedrawWindow();

		if (new_figure) {
			// Game Over
			if (!check_figure_position(fig_x, fig_y, figure)) {
				copyXPMArea(64, 64, 23, 15, 12, 24);
				RedrawWindow();
				while (1) {
					if (js_fd != -1) {
						struct js_event js;
						while (read(js_fd, &js, sizeof(struct js_event)) == sizeof(struct js_event)) {
							if (js.type & JS_EVENT_BUTTON) {
								// ボタンイベント処理
								if (js.value) { // ボタンが押された場合
									switch (js.number) {
										case 0: // ボタン0: 左回転
											// New Game
											goto NewGame_loop;
									}
								}
							}
						}
					}
					//Sleep
					usleep(100000);  // 100,000マイクロ秒 (0.1秒)

					XCheckMaskEvent(display,ButtonPressMask, &event);
					if (event.type == ButtonPress)
						break;
					if (event.type == Expose)
						RedrawWindow();

				}
				exit(0);
			}
		}

		for (step = 0; step < (fast_mode ? FAST_MODE_DELAY / TEMPORAL_RESOLUTION : INITIAL_DELAY / TEMPORAL_RESOLUTION); step++) {

			if (js_fd == -1) {
				// ジョイスティックなし
				if (replay_mode) {
					draw_figure(figure, -1, fig_x, fig_y);
					processJoystickInputFromFile(&fig_x, &fig_y, figure, figure_type, &fast_mode);
					draw_figure(figure, figure_type, fig_x, fig_y);
					RedrawWindow();
				} else {
					while (XPending(display)) {
						input = 0;

						XNextEvent(display, &event);
						switch (event.type) {
						case ButtonRelease:
							fast_mode = 0;
							break;
						case ButtonPress:
							if (!(input =
								which_button(event.xbutton.x, event.xbutton.y))) {
							}
						}

						if (input) {
							draw_figure(figure, -1, fig_x, fig_y);
							switch (input) {
							case BUTTON_ROTATE_LEFT:
								rotate_figure(0, figure, fig_x, fig_y);
								break;
							case BUTTON_ROTATE_RIGHT:
								rotate_figure(1, figure, fig_x, fig_y);
								break;
							case BUTTON_MOVE_LEFT:
								if (check_figure_position(fig_x - 1, fig_y, figure))
									fig_x--;
								break;
							case BUTTON_MOVE_RIGHT:
								if (check_figure_position(fig_x + 1, fig_y, figure))
									fig_x++;
								break;
							case BUTTON_MOVE_DOWN:
								fast_mode = 1;
								break;
							}
							draw_figure(figure, figure_type, fig_x, fig_y);
							RedrawWindow();
						}
					}
				}
			} else {
				// ジョイスティックあり
				draw_figure(figure, -1, fig_x, fig_y);
				processJoystickInput(&fig_x, &fig_y, figure, figure_type, &fast_mode);
				draw_figure(figure, figure_type, fig_x, fig_y);
				RedrawWindow();
			}
			usleep(TEMPORAL_RESOLUTION);
		}
	}

    if (js_fd != -1) {
        close(js_fd); // ジョイスティックデバイスのクローズ
    }
    return 0;
}

int which_button(int x, int y) {
	int i;
	
	for (i = BUTTONC - 1; i >= 0; i--) {
		if ((buttons[i][0] <= x && x < buttons[i][2]) &&
			(buttons[i][1] <= y && y < buttons[i][3]))
		 break;
	}
	return i;
}

int rotate_figure(int direction, int figure[4][2], int fig_x, int fig_y) {
	int i, temp[4][2];

	for (i = 0; i < 4; i++) {
		temp[i][0] = direction ? 3 - figure[i][1] :     figure[i][1];
		temp[i][1] = direction ?     figure[i][0] : 3 - figure[i][0];
	}
	if (check_figure_position(fig_x, fig_y, temp)) {
		for (i = 0; i < 4; i++) {
			figure[i][0] = temp[i][0];
			figure[i][1] = temp[i][1];
		}
		return 1;
	} else {
		return 0;
	}
}

void draw_figure(int figure[4][2], int type, int x, int y) {
	general_draw_figure(BOARD_POS_X, BOARD_POS_Y, figure, type, x, y);
}

void draw_next_figure(int figure[4][2], int type) {
	copyXPMArea(64 + NEXT_POS_X, NEXT_POS_Y, BLOCK_SIZE * 4, BLOCK_SIZE * 4,
				NEXT_POS_X, NEXT_POS_Y);
	general_draw_figure(NEXT_POS_X, NEXT_POS_Y, figure, type, 0, 0);
}

void general_draw_figure(int base_x, int base_y, int figure[4][2],
						 int type, int x, int y)
{
	int i, block_x, block_y;

	for (i = 0; i < 4; i++) {
		block_x = base_x + BLOCK_SIZE * (x + figure[i][0]);
		block_y = base_y + BLOCK_SIZE * (y + figure[i][1]);
		if (type == -1)
			copyXPMArea(64 + block_x, block_y, BLOCK_SIZE, BLOCK_SIZE, block_x, block_y);
		else
			copyXPMArea(0, 64 + BLOCK_SIZE * type, BLOCK_SIZE, BLOCK_SIZE, block_x, block_y);
	}
}

void full_refresh() {
	int x, y;

	for (y = 0; y < BOARD_HEIGHT; y++)
		for (x = 0; x < BOARD_WIDTH; x++)
			if (board[x][y])
				copyXPMArea(0, 64 + BLOCK_SIZE * (board[x][y] - 1), BLOCK_SIZE, BLOCK_SIZE,
							BOARD_POS_X + BLOCK_SIZE * x, BOARD_POS_Y + BLOCK_SIZE * y);
			else
				copyXPMArea(64 + BOARD_POS_X + BLOCK_SIZE * x, BOARD_POS_Y + BLOCK_SIZE * y, 
							BLOCK_SIZE, BLOCK_SIZE, 
							BOARD_POS_X + BLOCK_SIZE * x, BOARD_POS_Y + BLOCK_SIZE * y);
}

int check_figure_position(int fig_x, int fig_y, int figure[4][2]) {
	int i, x, y;

	for (i = 0; i < 4; i++) {
		x = fig_x + figure[i][0];
		y = fig_y + figure[i][1];
		if ((x < 0) || (x >= BOARD_WIDTH) || (y < 0) || (y >= BOARD_HEIGHT))
			return 0;
		if (board[x][y])
			return 0;
	}
	return 1;
}
