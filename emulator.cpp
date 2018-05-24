#include <stdio.h>
#include <stdlib.h>
#include <SDL/SDL.h>
#define SDL_MAIN_HANDLED
#include "unistd.h"
#include <fstream>
#include <assert.h>
#include <iostream>

const int MEMORY_SIZE = 4096;
const int REGISTER_SIZE = 16;
const int WIDTH = 64;
const int HEIGHT = 32;
const int GRAPHICS_SIZE = WIDTH * HEIGHT;
const int KEYBOARD_SIZE = 16;
const int STACK_SIZE = 16;
const int PROGRAM_START_LOCATION = 512;
bool keyStatus[256];

unsigned char CHIP8_FONTS[80] = {
	0xF0, 0x90, 0x90, 0x90, 0xF0,
	0x20, 0x60, 0x20, 0x20, 0x70,
	0xF0, 0x10, 0xF0, 0x80, 0xF0,
	0xF0, 0x10, 0xF0, 0x10, 0xF0,
	0x90, 0x90, 0xF0, 0x10, 0x10,
	0xF0, 0x80, 0xF0, 0x10, 0xF0,
	0xF0, 0x80, 0xF0, 0x90, 0xF0,
	0xF0, 0x10, 0x20, 0x40, 0x40,
	0xF0, 0x90, 0xF0, 0x90, 0xF0,
	0xF0, 0x90, 0xF0, 0x10, 0xF0,
	0xF0, 0x90, 0xF0, 0x90, 0x90,
	0xE0, 0x90, 0xE0, 0x90, 0xE0,
	0xF0, 0x80, 0x80, 0x80, 0xF0,
	0xE0, 0x90, 0x90, 0x90, 0xE0,
	0xF0, 0x80, 0xF0, 0x80, 0xF0,
	0xF0, 0x80, 0xF0, 0x80, 0x80,
};

/* for SDL
x goes from left to right
y goes from up to down
*/

class Chip8
{
private:
	unsigned char V[REGISTER_SIZE];
	// If a program includes sprite data, it should be padded so any instructions 
	// following it will be properly situated in RAM.
	unsigned char memory[MEMORY_SIZE];
	unsigned short I;
	unsigned short PC;
	unsigned char delayTimer, soundTimer;
	unsigned short stack[STACK_SIZE];
	unsigned short SP;
	unsigned char graphics[GRAPHICS_SIZE];
	unsigned char keyboard[KEYBOARD_SIZE];
	unsigned char opcode;
	// flags to help ease execution
	bool normalExecution;
	bool jumpExecution;
	bool noExecution;
	SDL_Surface *scrDisplay;

	void createDisplay() {
		SDL_Init(SDL_INIT_EVERYTHING);
		SDL_EnableUNICODE(1);
		this->scrDisplay = SDL_SetVideoMode(WIDTH * 10, HEIGHT * 10, 32, SDL_HWSURFACE);
	}

	void clearMemory() {
		for (int i = 0; i < MEMORY_SIZE; i++) this->memory[i] = 0;
	}

	void loadFonts() {
		for (int i = 0; i < 80; i++) {
			this->memory[i] = CHIP8_FONTS[i];
		}
	}

	void clearDisplay() {
		for (int i = 0; i < GRAPHICS_SIZE; i++) {
			this->graphics[i] = 0;
		}
	}

	void clearStack() {
		for (int i = 0; i < STACK_SIZE; i++) {
			this->stack[i] = 0;
		}
	}

	void clearKeyboard() {
		for (int i = 0; i < KEYBOARD_SIZE; i++) {
			this->keyboard[i] = 0;
		}
	}

	void resetTimers() {
		// Verify this
		this->delayTimer = 0, this->soundTimer = 0;
	}

	void updateTimers() {
		if (this->delayTimer > 0) this->delayTimer--;
		if (this->soundTimer > 0) this->soundTimer--;
	}

	// The interpreter sets the program counter to the address at the top of the stack, 
	// then subtracts 1 from the stack pointer.
	void returnFromSubroutine() {
		this->SP--;
		this->PC = this->stack[this->SP];
		this->jumpExecution = false, this->normalExecution = true, this->noExecution = false;
	}

	void updateProgramCounter(unsigned short NNN) {
		this->PC = NNN;
		this->jumpExecution = false, this->normalExecution = false, this->noExecution = true;
	}

	void callSubroutineAtAddr(unsigned short NNN) {
		this->stack[this->SP] = this->PC;
		this->SP++;
		this->PC = NNN;
		this->jumpExecution = false, this->normalExecution = false, this->noExecution = true;
	}

	void skipInstructionIfEqual(unsigned short X, unsigned char KK) {
		if (this->V[X] == KK) {
			this->jumpExecution = true, this->normalExecution = false, this->noExecution = false;
		} else {
			this->jumpExecution = false, this->normalExecution = true, this->noExecution = false;
		}
		return ;
	}

	void skipInstructionIfNotEqual(unsigned char X, unsigned char KK) {
		if (this->V[X] != KK) {
			this->jumpExecution = true, this->normalExecution = false, this->noExecution = false;
		} else {
			this->jumpExecution = false, this->normalExecution = true, this->noExecution = false;
		}
		return ;
	}

	void skipIfRegisterValuesEqual(unsigned char X, unsigned char Y) {
		if (this->V[X] == this->V[Y]) {
			this->jumpExecution = true, this->normalExecution = false, this->noExecution = false;
		} else {
			this->jumpExecution = false, this->normalExecution = true, this->noExecution = false;
		}
		return ;
	}

	void loadValueIntoRegister(unsigned char X, unsigned char KK) {
		this->V[X] = KK;
		return ;
	}

	void addValueIntoRegister(unsigned char X, unsigned char KK) {
		this->V[X] += KK;
		return ;
	}

	void setValueInRegisterFromRegister(unsigned char X, unsigned char Y) {
		this->V[X] = this->V[Y];
		return ;
	}

	void orOperationOnRegister(unsigned char X, unsigned char Y) {
		this->V[X] = (this->V[X] | this->V[Y]);
		return ;
	}

	void andOperationOnRegister(unsigned char X, unsigned char Y) {
		this->V[X] = (this->V[X] & this->V[Y]);
		return ;
	}

	void xorOperationOnRegister(unsigned char X, unsigned char Y) {
		this->V[X] = (this->V[X] ^ this->V[Y]);
		return ;
	}

	void addOperationOnRegister(unsigned char X, unsigned char Y) {
		if (this->V[Y] > 0xFF - this->V[Y]) {
			this->V[0xF] = 1;
		} else {
			this->V[0xF] = 0;
		}
		this->V[X] += this->V[Y];
		return ;
	}

	void subtractOperationOnRegister(unsigned char X, unsigned char Y) {
		if (this->V[Y] > this->V[X]) {
			this->V[0xF] = 0;
		} else {
			this->V[0xF] = 1;
		}
		this->V[X] -= this->V[Y];
		return ;
	}

	void shrOperationOnRegister(unsigned char X, unsigned char Y = 0) {
		if (this->V[X] & 1) {
			this->V[0xF] = 1;
		} else {
			this->V[0xF] = 0;
		}
		this->V[X] = (this->V[X] >> 1);
		return ;
	}

	void subnOperationOnRegister(unsigned char X, unsigned char Y) {
		if (this->V[X] > this->V[Y]) {
			this->V[0xF] = 1;
		} else {
			this->V[0xF] = 0;
		}
		this->V[X] = this->V[Y] - this->V[X];
		return ;
	}

	void shlOperationOnRegister(unsigned char X, unsigned char Y = 0) {
		if (this->V[X] & (1<<7)) {
			this->V[0xF] = 1;
		} else {
			this->V[0xF] = 0;
		}
		this->V[X] = (this->V[X] << 1);
		return ;
	}

	void skipNextInstruction(unsigned char X, unsigned char Y) {
		if (this->V[X] != this->V[Y]) {
			this->jumpExecution = true, this->normalExecution = false, this->noExecution = false;
		} else {
			this->jumpExecution = false, this->normalExecution = true, this->noExecution = false;
		}
		return ;
	}

	void loadAddressIntoI(unsigned short NNN) {
		this->I = NNN;
		return ;
	}

	void jumpToAddressWithOffset(unsigned short NNN) {
		this->PC = NNN + this->V[0x0];
		this->jumpExecution = false, this->normalExecution = false, this->noExecution = true;
		return ;
	}

	void rndValueIntoRegsiter(unsigned char X, unsigned char KK) {
		unsigned char rndValue = rand() % 256;
		this->V[X] = (KK & rndValue);
		return ;
	}

	void diplaySprite(unsigned char N, unsigned char X, unsigned char Y) {
		unsigned char X1, Y1;
		for (int i = 0; i < N; i++) {
			unsigned char memoryAtLocation = this->memory[this->I + i];
			for (int bitIndex = 0; bitIndex < 8; bitIndex++) {
				// draw at V[X] + bitIndex, V[Y] + i
				unsigned char xLocation = (this->V[X] + bitIndex) % WIDTH;
				unsigned char yLocation = (this->V[Y] + i) % HEIGHT;
				if (memoryAtLocation & (0x80 >>bitIndex)) {
					int pixelLocation = (xLocation + yLocation * WIDTH);
					if (this->graphics[pixelLocation]) {
						this->V[0xF] = 1;
					}
					this->graphics[pixelLocation] ^= 1;
				}
			}
		}
	}

	void skipInstrcutionIfKeyNotPressed(unsigned char X) {
		if (!keyStatus[this->V[X]]) {
			this->jumpExecution = true, this->normalExecution = false, this->noExecution = false;
		}
		return ;
	}

	void skipInstructionIfKeyPressed(unsigned char X) {
		if (keyStatus[this->V[X]]) {
			this->jumpExecution = true, this->normalExecution = false, this->noExecution = false;
		}
		return ;
	}

	void readRegsitersFromMemory(unsigned char X) {
		for (int i = 0; i <= X; i++) {
			this->V[i] = this->memory[this->I + i];
		}
		return ;
	}

	void storeRegisterIntoMemory(unsigned char X) {
		for (int i = 0; i <= X; i++) {
			this->memory[this->I + i] = this->V[i];
		}
		return ;
	}

	void storeBCDRepresentation(unsigned char X) {
		int num = this->V[X];
		this->memory[I + 2] = num % 10; num /= 10;
		this->memory[I + 1] = num % 10; num /= 10;
		this->memory[I] = num;
		return ;
	}

	void locationOfSprite(unsigned char X) {
		this->I = this->V[X] * 5;
		return ;
	}

	void addRegisterToI(unsigned char X) {
		this->I += this->V[X];
		return ;
	}

	void setSoundTimer(unsigned char X) {
		this->soundTimer = this->V[X];
		return ;
	}

	void setDelayTimer(unsigned char X) {
		this->delayTimer = this->V[X];
		return ;
	}

	void waitForKeyPress(unsigned char X) {
		bool shouldWeProceed = false;
		for (int i = 1; i <= 15; i++) {
			shouldWeProceed |= keyStatus[i];
		}
		if (shouldWeProceed) {
			this->jumpExecution = false, this->normalExecution = true, this->noExecution = false;
		} else {
			this->jumpExecution = false, this->normalExecution = false, this->noExecution = true;
		}
		return ;
	}

	void storeDelayTimer(unsigned char X) {
		this->V[X] = this->delayTimer;
		return ;
	}

	void createDelay() {
		usleep(10000); return ;
	}

	void drawGraphics() {
		SDL_Rect rect;
		for (int i = 0; i < WIDTH; i++) {
			for (int j = 0; j < HEIGHT; j++) {
				rect.x = i*10, rect.y = j*10, rect.w = 10, rect.h = 10;
				if (this->graphics[i + j * WIDTH]) {
					SDL_FillRect(this->scrDisplay, &rect, SDL_MapRGB(this->scrDisplay->format, 255, 255, 255));
				} else {
					SDL_FillRect(this->scrDisplay, &rect, SDL_MapRGB(this->scrDisplay->format, 0, 0, 0));
				}
			}
		}
		SDL_Flip(this->scrDisplay);
		// std::cout << "Drawing graphics complete\n";
	}

	/* For keypad we have the following mapping:
	1-7, 2-8, 3-9, c-0
	4-u, 5-i, 6-o, d-p
	7-j, 8-k, 9-l, e-;
	a-m, 0-,, b-., f-/ */
	void takeInput() {
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			if (event.type == SDL_KEYDOWN) {
				switch (event.key.keysym.sym) {
					case SDLK_7: keyStatus[0x1] = 1; break;
					case SDLK_8: keyStatus[0x2] = 1; break;
					case SDLK_9: keyStatus[0x3] = 1; break;
					case SDLK_0: keyStatus[0xC] = 1; break;
					case SDLK_u: keyStatus[0x4] = 1; break;
					case SDLK_i: keyStatus[0x5] = 1; break;
					case SDLK_o: keyStatus[0x6] = 1; break;
					case SDLK_p: keyStatus[0xD] = 1; break;
					case SDLK_j: keyStatus[0x7] = 1; break;
					case SDLK_k: keyStatus[0x8] = 1; break;
					case SDLK_l: keyStatus[0x9] = 1; break;
					case SDLK_SEMICOLON: keyStatus[0xE] = 1; break;
					case SDLK_m: keyStatus[0xA] = 1; break;
					case SDLK_COMMA: keyStatus[0x0] = 1; break;
					case SDLK_PERIOD: keyStatus[0xB] = 1; break;
					case SDLK_SLASH: keyStatus[0xF] = 1; break;
				}
			} else if (event.type == SDL_KEYUP) {
				switch (event.key.keysym.sym) {
					case SDLK_7: keyStatus[0x1] = 0; break;
					case SDLK_8: keyStatus[0x2] = 0; break;
					case SDLK_9: keyStatus[0x3] = 0; break;
					case SDLK_0: keyStatus[0xC] = 0; break;
					case SDLK_u: keyStatus[0x4] = 0; break;
					case SDLK_i: keyStatus[0x5] = 0; break;
					case SDLK_o: keyStatus[0x6] = 0; break;
					case SDLK_p: keyStatus[0xD] = 0; break;
					case SDLK_j: keyStatus[0x7] = 0; break;
					case SDLK_k: keyStatus[0x8] = 0; break;
					case SDLK_l: keyStatus[0x9] = 0; break;
					case SDLK_SEMICOLON: keyStatus[0xE] = 0; break;
					case SDLK_m: keyStatus[0xA] = 0; break;
					case SDLK_COMMA: keyStatus[0x0] = 0; break;
					case SDLK_PERIOD: keyStatus[0xB] = 0; break;
					case SDLK_SLASH: keyStatus[0xF] = 0; break;
				}
			}
		}
	}
public:
	void initialize() {
		this->PC = 0x200;
		this->opcode = 0;
		this->I = 0;
		this->SP = 0;
		this->clearMemory();
		this->normalExecution = this->jumpExecution = this->noExecution = false;
		// We already have the interpreter so only fonts need to be loaded
		this->loadFonts();
		this->clearDisplay();
		this->clearStack();
		this->clearKeyboard();
		this->resetTimers();
		this->createDisplay();
		srand(time(NULL));
	}

	void loadGame() {
		FILE *source;
		source = fopen("PONG.bin", "rb");
		if (source != NULL) {
			fseek(source, 0, SEEK_END);
			size_t fileSize = ftell(source);
			rewind(source);
			size_t result = fread(this->memory + PROGRAM_START_LOCATION, 1, fileSize, source);
			if (result != fileSize) {
				std::cout << "There was an error while reading the pong file.\n";
				exit(0);
			} else {
				std::cout << "ROM loaded successfully\n";
			}
			fclose(source);
		} else {
			std::cout << "There was an error while reading the pong file.\n";
		}
		return ;
	}

	// All references from http://devernay.free.fr/hacks/chip8/C8TECH10.HTM
	void emulateCycle() {
		// std::cout << "Starting one cycle of operation\n";
		this->takeInput();
		this->drawGraphics();
		// TODO : test this part of the code
		unsigned short opcode = (this->memory[this->PC] << 8) | this->memory[this->PC + 1];
		unsigned char X = (opcode & 0x0F00) >> 8;
		unsigned char Y = (opcode & 0x00F0) >> 4;
		unsigned short NNN = opcode & 0x0FFF;
		unsigned char NN = (opcode & 0x00FF); 
		unsigned char KK = (opcode & 0x00FF);
		unsigned char N = (opcode & 0x000F);
		// Default is normal execution unless we have a jump instruction or
		// no operation
		this->normalExecution = true, this->jumpExecution = false, this->noExecution = false;
		switch (opcode & 0xF000) {
			case 0x0000:
				if (opcode == 0x00E0) {
					std::cout << "OPCODE : 0x00E0\n";
					this->clearDisplay();
				} else if (opcode == 0x00EE) {
					std::cout << "OPCODE : 0x00EE\n";
					this->returnFromSubroutine();
				}
				break;
			case 0x1000:
				std::cout << "OPCODE : 0x1000\n";
				this->updateProgramCounter(NNN);
				break;
			case 0x2000:
				std::cout << "OPCODE : 0x2000\n";
				this->callSubroutineAtAddr(NNN);
				break;
			case 0x3000:
				std::cout << "OPCODE : 0x3000\n";
				this->skipInstructionIfEqual(X, KK);
				break;
			case 0x4000:
				std::cout << "OPCODE : 0x4000\n";
				this->skipInstructionIfNotEqual(X, KK);
				break;
			case 0x5000:
				std::cout << "OPCODE : 0x5000\n";
				this->skipIfRegisterValuesEqual(X, Y);
				break;
			case 0x6000:
				std::cout << "OPCODE : 0x6000\n";
				this->loadValueIntoRegister(X, KK);
				break;
			case 0x7000:
				std::cout << "OPCODE : 0x7000\n";
				this->addValueIntoRegister(X, KK);
				break;
			case 0x8000:
				std::cout << "OPCODE : 0x8000\n";
				switch (opcode & 0x000F) {
					case 0x0000:
						this->setValueInRegisterFromRegister(X, Y);
						break;
					case 0x0001:
						this->orOperationOnRegister(X, Y);
						break;
					case 0x0002:
						this->andOperationOnRegister(X, Y);
						break;
					case 0x0003:
						this->xorOperationOnRegister(X, Y);
						break;
					case 0x0004:
						this->addOperationOnRegister(X, Y);
						break;
					case 0x0005:
						this->subtractOperationOnRegister(X, Y);
						break;
					case 0x0006:
						this->shrOperationOnRegister(X, Y);
						break;
					case 0x0007:
						this->subnOperationOnRegister(X, Y);
						break;
					case 0x000E:
						this->shlOperationOnRegister(X, Y);
						break;
				}
				break;
			case 0x9000:
				std::cout << "OPCODE : 0x9000\n";
				this->skipInstructionIfNotEqual(X, Y);
				break;
			case 0xA000:
				std::cout << "OPCODE : 0xA000\n";
				this->loadAddressIntoI(NNN);
				break;
			case 0xB000:
				std::cout << "OPCODE : 0xB000\n";
				this->jumpToAddressWithOffset(NNN);
				break;
			case 0xC000:
				std::cout << "OPCODE : 0xC000\n";
				this->rndValueIntoRegsiter(X, KK);
				break;
			case 0xD000:
				std::cout << "OPCODE : 0xD000\n";
				this->diplaySprite(N, X, Y);
				break;
			case 0xE000:
				std::cout << "OPCODE : 0xE000\n";
				switch (opcode & 0x00F0) {
					case 0x0090:
						this->skipInstructionIfKeyPressed(X);
						break;
					case 0x00A0:
						this->skipInstrcutionIfKeyNotPressed(X);
						break;
				}
				break;
			case 0xF000:
				std::cout << "OPCODE : 0xF000\n";
				switch (opcode & 0x00FF) {
					case 0x0007:
						this->storeDelayTimer(X);
						break;
					case 0x000A:
						this->waitForKeyPress(X);
						break;
					case 0x0015:
						this->setDelayTimer(X);
						break;
					case 0x0018:
						this->setSoundTimer(X);
						break;
					case 0x001E:
						this->addRegisterToI(X);
						break;
					case 0x0029:
						this->locationOfSprite(X);
						break;
					case 0x0033:
						this->storeBCDRepresentation(X);
						break;
					case 0x0055:
						this->storeRegisterIntoMemory(X);
						break;
					case 0x0065:
						this->readRegsitersFromMemory(X);
						break;
				}
				break;
			default:
				std::cout << "oh my are we fucked\n";
				break;
		}
		// std::cout << "Are we here\n";
		if (this->normalExecution) this->normalExecution = false, this->PC += 2;
		if (this->jumpExecution) this->jumpExecution = false, this->PC += 4;
		if (this->noExecution) this->noExecution = false;
		this->updateTimers();
		this->createDelay();
	}
};

int main() {
	// initiate SDL
	SDL_Init(SDL_INIT_EVERYTHING);
	SDL_EnableUNICODE(1);
	SDL_Surface *screen = NULL;
	screen = SDL_SetVideoMode(WIDTH * 10, HEIGHT * 10, 32, SDL_HWSURFACE);
	Chip8 *cpu = new Chip8();
	cpu->initialize();
	cpu->loadGame();
	while (true) {
		cpu->emulateCycle();
	}
return 0;}