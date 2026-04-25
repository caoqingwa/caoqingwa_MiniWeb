const canvas = document.getElementById('tetris');
const context = canvas.getContext('2d');
context.scale(20, 20);

const matrix = [
    [0, 0, 0],
    [1, 1, 1],
    [0, 1, 0],
];

function arenaSweep() {
    outer: for (let y = arena.length - 1; y > 0; --y) {
        for (let x = 0; x < arena[y].length; ++x) {
            if (arena[y][x] === 0) {
                continue outer;
            }
        }
        const row = arena.splice(y, 1)[0].fill(0);
        arena.unshift(row);
        ++y;
    }
}

function collide(arena, player) {
    const m = player.matrix;
    const o = player.pos;
    for (let y = 0; y < m.length; ++y) {
        for (let x = 0; x < m[y].length; ++x) {
            if (m[y][x] !== 0 &&
                (arena[y + o.y] &&
                 arena[y + o.y][x + o.x]) !== 0) {
                return true;
            }
        }
    }
    return false;
}

function createMatrix(w, h) {
    const matrix = [];
    while (h--) {
        matrix.push(new Array(w).fill(0));
    }
    return matrix;
}

function createPiece(type) {
    if (type === 'T') {
        return [
            [0, 0, 0],
            [1, 1, 1],
            [0, 1, 0],
        ];
    } else if (type === 'O') {
        return [
            [2, 2],
            [2, 2],
        ];
    } else if (type === 'L') {
        return [
            [0, 3, 0],
            [0, 3, 0],
            [0, 3, 3],
        ];
    } else if (type === 'J') {
        return [
            [0, 4, 0],
            [0, 4, 0],
            [4, 4, 0],
        ];
    } else if (type === 'I') {
        return [
            [0, 5, 0, 0],
            [0, 5, 0, 0],
            [0, 5, 0, 0],
            [0, 5, 0, 0],
        ];
    } else if (type === 'S') {
        return [
            [0, 6, 6],
            [6, 6, 0],
            [0, 0, 0],
        ];
    } else if (type === 'Z') {
        return [
            [7, 7, 0],
            [0, 7, 7],
            [0, 0, 0],
        ];
    }
}

function drawMatrix(matrix, offset, outline = false) {
    context.save();

    matrix.forEach((row, y) => {
        row.forEach((value, x) => {
            if (value !== 0) {
                context.fillStyle = colors[value];
                context.fillRect(x + offset.x, y + offset.y, 1, 1);
            }
        });
    });

    if (!outline) {
        context.restore();
        return;
    }

    context.lineWidth = 0.08;
    context.strokeStyle = 'rgba(255,255,255,0.95)';
    context.lineJoin = 'round';
    context.lineCap = 'round';

    matrix.forEach((row, y) => {
        row.forEach((value, x) => {
            if (value === 0) {
                return;
            }

            const cellX = x + offset.x;
            const cellY = y + offset.y;

            if (!matrix[y - 1] || matrix[y - 1][x] === 0) {
                context.beginPath();
                context.moveTo(cellX, cellY);
                context.lineTo(cellX + 1, cellY);
                context.stroke();
            }

            if (!matrix[y + 1] || matrix[y + 1][x] === 0) {
                context.beginPath();
                context.moveTo(cellX, cellY + 1);
                context.lineTo(cellX + 1, cellY + 1);
                context.stroke();
            }

            if (!row[x - 1] || row[x - 1] === 0) {
                context.beginPath();
                context.moveTo(cellX, cellY);
                context.lineTo(cellX, cellY + 1);
                context.stroke();
            }

            if (!row[x + 1] || row[x + 1] === 0) {
                context.beginPath();
                context.moveTo(cellX + 1, cellY);
                context.lineTo(cellX + 1, cellY + 1);
                context.stroke();
            }
        });
    });

    context.restore();
}

function drawGrid() {
    context.save();
    context.strokeStyle = 'rgba(255,255,255,0.3)'; // 透明度
    context.lineWidth = 0.08; // 更粗
    for (let x = 0; x <= 12; x++) {
        context.beginPath();
        context.moveTo(x, 0);
        context.lineTo(x, 20);
        context.stroke();
    }
    for (let y = 0; y <= 20; y++) {
        context.beginPath();
        context.moveTo(0, y);
        context.lineTo(12, y);
        context.stroke();
    }
    context.restore();
}

function draw() {
    context.fillStyle = '#000';
    context.fillRect(0, 0, canvas.width, canvas.height);
    drawGrid();
    drawMatrix(arena, {x: 0, y: 0});
    drawMatrix(player.matrix, player.pos, true);
}

function merge(arena, player) {
    player.matrix.forEach((row, y) => {
        row.forEach((value, x) => {
            if (value !== 0) {
                arena[y + player.pos.y][x + player.pos.x] = value;
            }
        });
    });
}

function playerDrop() {
    player.pos.y++;
    if (collide(arena, player)) {
        player.pos.y--;
        merge(arena, player);
        playerReset();
        arenaSweep();
    }
    dropCounter = 0;
}

function playerMove(dir) {
    player.pos.x += dir;
    if (collide(arena, player)) {
        player.pos.x -= dir;
    }
}

function playerReset() {
    const pieces = 'TJLOSZI';
    player.matrix = createPiece(pieces[pieces.length * Math.random() | 0]);
    player.pos.y = 0;
    player.pos.x = (arena[0].length / 2 | 0) - (player.matrix[0].length / 2 | 0);
    if (collide(arena, player)) {
        arena.forEach(row => row.fill(0));
    }
}

function playerRotate(dir) {
    const pos = player.pos.x;
    let offset = 1;
    rotate(player.matrix, dir);
    while (collide(arena, player)) {
        player.pos.x += offset;
        offset = -(offset + (offset > 0 ? 1 : -1));
        if (offset > player.matrix[0].length) {
            rotate(player.matrix, -dir);
            player.pos.x = pos;
            return;
        }
    }
}

function rotate(matrix, dir) {
    for (let y = 0; y < matrix.length; ++y) {
        for (let x = 0; x < y; ++x) {
            [matrix[x][y], matrix[y][x]] = [matrix[y][x], matrix[x][y]];
        }
    }
    if (dir > 0) {
        matrix.forEach(row => row.reverse());
    } else {
        matrix.reverse();
    }
}

let dropCounter = 0;
let dropInterval = 1000;
let lastTime = 0;

function update(time = 0) {
    const deltaTime = time - lastTime;
    lastTime = time;
    dropCounter += deltaTime;
    if (dropCounter > dropInterval) {
        playerDrop();
    }
    draw();
    requestAnimationFrame(update);
}

const colors = [
    null,
    '#FF0D72', // T
    '#0DC2FF', // O
    '#0DFF72', // L
    '#F538FF', // J
    '#FF8E0D', // I
    '#FFE138', // S
    '#3877FF', // Z
];

const arena = createMatrix(12, 20);

const player = {
    pos: {x: 0, y: 0},
    matrix: null,
};

playerReset();

document.addEventListener('keydown', event => {
    if (event.key === 'ArrowLeft') {
        playerMove(-1);
    } else if (event.key === 'ArrowRight') {
        playerMove(1);
    } else if (event.key === 'ArrowDown') {
        playerDrop();
    } else if (event.key === 'q' || event.key === 'Q') {
        playerRotate(-1);
    } else if (event.key === 'e' || event.key === 'E') {
        playerRotate(1);
    }
});

update();
