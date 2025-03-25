#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Enums for Flex properties
typedef enum {
    DIR_ROW, DIR_ROW_REVERSE, DIR_COLUMN, DIR_COLUMN_REVERSE
} FlexDirection;

typedef enum {
    WRAP_NOWRAP, WRAP_WRAP, WRAP_WRAP_REVERSE
} FlexWrap;

typedef enum {
    JUSTIFY_START, JUSTIFY_END, JUSTIFY_CENTER, 
    JUSTIFY_SPACE_BETWEEN, JUSTIFY_SPACE_AROUND, JUSTIFY_SPACE_EVENLY
} JustifyContent;

typedef enum {
    ALIGN_START, ALIGN_END, ALIGN_CENTER, ALIGN_BASELINE, ALIGN_STRETCH
} AlignType;

typedef enum {
    VIS_VISIBLE, VIS_HIDDEN, VIS_COLLAPSE
} Visibility;

typedef enum {
    POS_STATIC, POS_ABSOLUTE
} PositionType;

// Structs
typedef struct {
    float x, y;
} Point;

typedef struct {
    float width, height;
    float minWidth, maxWidth;
    float minHeight, maxHeight;
    float flexGrow, flexShrink;
    float flexBasis;
    float margin[4];  // top, right, bottom, left
    AlignType alignSelf;
    int order;
    Visibility visibility;
    PositionType position;
    float aspectRatio;  // 0 if not set
    Point positionCoords;
} FlexItem;

typedef struct {
    FlexItem* items;
    int itemCount;
    float totalBaseSize;
    float height;
} FlexLine;

typedef struct {
    float width, height;
    FlexDirection direction;
    FlexWrap wrap;
    JustifyContent justify;
    AlignType alignItems;
    AlignType alignContent;
    float gap;
    FlexItem* items;
    int itemCount;
    char* writingMode;  // Simplified as string
    char* textDirection;
} FlexContainer;

// Helper Functions
float clamp(float value, float min, float max) {
    if (max != 0) return fmin(fmax(value, min), max);
    return fmax(value, min);
}

float resolveFlexBasis(FlexItem* item) {
    if (item->flexBasis > 0) return item->flexBasis;
    return item->width;  // Simplified: assumes content size = width
}

void applyConstraints(FlexItem* item) {
    item->width = clamp(item->width, item->minWidth, item->maxWidth);
    item->height = clamp(item->height, item->minHeight, item->maxHeight);
}

// Main Layout Function
void layoutFlexContainer(FlexContainer* container) {
    // Determine axes
    int isRow = (container->direction == DIR_ROW || container->direction == DIR_ROW_REVERSE);
    int isReverse = (container->direction == DIR_ROW_REVERSE || container->direction == DIR_COLUMN_REVERSE);
    float mainSize = isRow ? container->width : container->height;
    float crossSize = isRow ? container->height : container->width;

    // Filter and sort items
    FlexItem* layoutItems = malloc(container->itemCount * sizeof(FlexItem));
    int layoutCount = 0;
    for (int i = 0; i < container->itemCount; i++) {
        if (container->items[i].position == POS_ABSOLUTE) {
            // Simplified absolute positioning
            container->items[i].positionCoords.x = 0;
            container->items[i].positionCoords.y = 0;
        } else if (container->items[i].visibility != VIS_HIDDEN) {
            layoutItems[layoutCount++] = container->items[i];
        }
    }

    // Sort by order (bubble sort for simplicity)
    for (int i = 0; i < layoutCount - 1; i++) {
        for (int j = 0; j < layoutCount - i - 1; j++) {
            if (layoutItems[j].order > layoutItems[j + 1].order) {
                FlexItem temp = layoutItems[j];
                layoutItems[j] = layoutItems[j + 1];
                layoutItems[j + 1] = temp;
            }
        }
    }

    // Calculate base sizes
    for (int i = 0; i < layoutCount; i++) {
        layoutItems[i].width = resolveFlexBasis(&layoutItems[i]);
        applyConstraints(&layoutItems[i]);
    }

    // Line breaking
    FlexLine* lines = NULL;
    int lineCount = 0;
    FlexLine currentLine = { .items = NULL, .itemCount = 0, .totalBaseSize = 0 };
    float remainingSpace = mainSize;

    for (int i = 0; i < layoutCount; i++) {
        float itemSize = layoutItems[i].width + container->gap;
        if (container->wrap == WRAP_NOWRAP || remainingSpace >= itemSize) {
            currentLine.items = realloc(currentLine.items, 
                                      (currentLine.itemCount + 1) * sizeof(FlexItem));
            currentLine.items[currentLine.itemCount++] = layoutItems[i];
            currentLine.totalBaseSize += itemSize;
            remainingSpace -= itemSize;
        } else {
            lines = realloc(lines, (lineCount + 1) * sizeof(FlexLine));
            lines[lineCount++] = currentLine;
            currentLine = (FlexLine){ .items = malloc(sizeof(FlexItem)), 
                                    .itemCount = 1, 
                                    .totalBaseSize = itemSize };
            currentLine.items[0] = layoutItems[i];
            remainingSpace = mainSize - itemSize;
        }
    }
    if (currentLine.itemCount > 0) {
        lines = realloc(lines, (lineCount + 1) * sizeof(FlexLine));
        lines[lineCount++] = currentLine;
    }

    // Process each line
    for (int l = 0; l < lineCount; l++) {
        FlexLine* line = &lines[l];
        float freeSpace = mainSize - line->totalBaseSize;
        float totalGrow = 0, totalShrink = 0;
        
        for (int i = 0; i < line->itemCount; i++) {
            totalGrow += line->items[i].flexGrow;
            totalShrink += line->items[i].flexShrink;
        }

        // Distribute space
        for (int i = 0; i < line->itemCount; i++) {
            if (freeSpace > 0 && totalGrow > 0) {
                line->items[i].width += (line->items[i].flexGrow / totalGrow) * freeSpace;
            } else if (freeSpace < 0 && totalShrink > 0) {
                line->items[i].width -= (line->items[i].flexShrink / totalShrink) * fabs(freeSpace);
            }
            applyConstraints(&line->items[i]);
        }

        // Position along main axis
        float mainPos = 0;
        switch (container->justify) {
            case JUSTIFY_END: mainPos = freeSpace; break;
            case JUSTIFY_CENTER: mainPos = freeSpace / 2; break;
            case JUSTIFY_SPACE_AROUND: mainPos = freeSpace / (line->itemCount * 2); break;
            case JUSTIFY_SPACE_EVENLY: mainPos = freeSpace / (line->itemCount + 1); break;
            default: break;
        }

        for (int i = 0; i < line->itemCount; i++) {
            int idx = isReverse ? line->itemCount - 1 - i : i;
            if (isRow) {
                line->items[idx].positionCoords.x = mainPos;
            } else {
                line->items[idx].positionCoords.y = mainPos;
            }
            mainPos += line->items[idx].width + container->gap;
            line->height = fmax(line->height, line->items[idx].height);
        }

        // Cross axis positioning
        for (int i = 0; i < line->itemCount; i++) {
            AlignType align = (line->items[i].alignSelf == ALIGN_STRETCH) ? 
                            container->alignItems : line->items[i].alignSelf;
            float crossPos = 0;
            switch (align) {
                case ALIGN_END: crossPos = crossSize - line->items[i].height; break;
                case ALIGN_CENTER: crossPos = (crossSize - line->items[i].height) / 2; break;
                default: break;
            }
            if (isRow) {
                line->items[i].positionCoords.y = crossPos;
            } else {
                line->items[i].positionCoords.x = crossPos;
            }
        }
    }

    // Clean up
    free(layoutItems);
    for (int i = 0; i < lineCount; i++) {
        free(lines[i].items);
    }
    free(lines);
}

// Example usage
int _main() {
    FlexContainer container = {
        .width = 800, .height = 600,
        .direction = DIR_ROW,
        .wrap = WRAP_WRAP,
        .justify = JUSTIFY_SPACE_BETWEEN,
        .alignItems = ALIGN_CENTER,
        .gap = 10,
        .items = malloc(3 * sizeof(FlexItem)),
        .itemCount = 3,
        .writingMode = "horizontal-tb",
        .textDirection = "ltr"
    };

    container.items[0] = (FlexItem){ .width = 200, .height = 100, .flexGrow = 1 };
    container.items[1] = (FlexItem){ .width = 200, .height = 150, .flexGrow = 2 };
    container.items[2] = (FlexItem){ .width = 200, .height = 200, .flexShrink = 1 };

    layoutFlexContainer(&container);

    // Print results
    for (int i = 0; i < container.itemCount; i++) {
        printf("Item %d: x=%.1f, y=%.1f, w=%.1f, h=%.1f\n",
               i, container.items[i].positionCoords.x, container.items[i].positionCoords.y,
               container.items[i].width, container.items[i].height);
    }

    free(container.items);
    return 0;
}