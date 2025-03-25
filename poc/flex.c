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
    char* writingMode;
    char* textDirection;
} FlexContainer;

// Helper Functions
float clamp(float value, float min, float max) {
    if (max != 0) return fmin(fmax(value, min), max);
    return fmax(value, min);
}

float resolveFlexBasis(FlexItem* item) {
    if (item->flexBasis > 0) return item->flexBasis;
    return item->width;
}

void applyConstraints(FlexItem* item) {
    item->width = clamp(item->width, item->minWidth, item->maxWidth);
    item->height = clamp(item->height, item->minHeight, item->maxHeight);
}

void layoutFlexContainer(FlexContainer* container) {
    int isRow = (container->direction == DIR_ROW || container->direction == DIR_ROW_REVERSE);
    int isReverse = (container->direction == DIR_ROW_REVERSE || container->direction == DIR_COLUMN_REVERSE);
    float mainSize = isRow ? container->width : container->height;
    float crossSize = isRow ? container->height : container->width;

    // Filter and copy items
    FlexItem* layoutItems = malloc(container->itemCount * sizeof(FlexItem));
    int layoutCount = 0;
    for (int i = 0; i < container->itemCount; i++) {
        if (container->items[i].position == POS_ABSOLUTE) {
            container->items[i].positionCoords = (Point){0, 0};
        } else if (container->items[i].visibility != VIS_HIDDEN) {
            layoutItems[layoutCount] = container->items[i];
            layoutItems[layoutCount].positionCoords = (Point){0, 0};  // Initialize position
            layoutCount++;
        }
    }

    // Sort by order
    for (int i = 0; i < layoutCount - 1; i++) {
        for (int j = 0; j < layoutCount - i - 1; j++) {
            if (layoutItems[j].order > layoutItems[j + 1].order) {
                FlexItem temp = layoutItems[j];
                layoutItems[j] = layoutItems[j + 1];
                layoutItems[j + 1] = temp;
            }
        }
    }

    // Resolve base sizes
    for (int i = 0; i < layoutCount; i++) {
        layoutItems[i].width = resolveFlexBasis(&layoutItems[i]);
        applyConstraints(&layoutItems[i]);
    }

    // Line breaking
    FlexLine* lines = NULL;
    int lineCount = 0;
    FlexLine currentLine = { .items = NULL, .itemCount = 0, .totalBaseSize = 0, .height = 0 };
    float remainingSpace = mainSize;

    for (int i = 0; i < layoutCount; i++) {
        float itemSize = layoutItems[i].width;
        float spaceNeeded = itemSize + (currentLine.itemCount > 0 ? container->gap : 0);
        
        if (container->wrap == WRAP_NOWRAP || remainingSpace >= spaceNeeded) {
            currentLine.items = realloc(currentLine.items, 
                                      (currentLine.itemCount + 1) * sizeof(FlexItem));
            currentLine.items[currentLine.itemCount++] = layoutItems[i];
            currentLine.totalBaseSize += spaceNeeded;
            currentLine.height = fmax(currentLine.height, layoutItems[i].height);
            remainingSpace -= spaceNeeded;
        } else {
            if (currentLine.itemCount > 0) {
                lines = realloc(lines, (lineCount + 1) * sizeof(FlexLine));
                lines[lineCount++] = currentLine;
            }
            currentLine = (FlexLine){ .items = malloc(sizeof(FlexItem)), 
                                    .itemCount = 1, 
                                    .totalBaseSize = itemSize, 
                                    .height = layoutItems[i].height };
            currentLine.items[0] = layoutItems[i];
            remainingSpace = mainSize - itemSize;
        }
    }
    if (currentLine.itemCount > 0) {
        lines = realloc(lines, (lineCount + 1) * sizeof(FlexLine));
        lines[lineCount++] = currentLine;
    }

    // Process each line
    float crossPos = 0;
    for (int l = 0; l < lineCount; l++) {
        FlexLine* line = &lines[l];
        float freeSpace = mainSize - line->totalBaseSize;
        float totalGrow = 0, totalShrink = 0;
        
        for (int i = 0; i < line->itemCount; i++) {
            totalGrow += line->items[i].flexGrow;
            totalShrink += line->items[i].flexShrink;
        }

        // Distribute space
        if (freeSpace > 0 && totalGrow > 0) {
            for (int i = 0; i < line->itemCount; i++) {
                line->items[i].width += (line->items[i].flexGrow / totalGrow) * freeSpace;
                applyConstraints(&line->items[i]);
            }
        } else if (freeSpace < 0 && totalShrink > 0) {
            for (int i = 0; i < line->itemCount; i++) {
                float shrinkFactor = line->items[i].flexShrink / totalShrink;
                line->items[i].width -= shrinkFactor * fabs(freeSpace);
                applyConstraints(&line->items[i]);
            }
        }

        // Recalculate line size after flex adjustments
        line->totalBaseSize = 0;
        for (int i = 0; i < line->itemCount; i++) {
            line->totalBaseSize += line->items[i].width + (i > 0 ? container->gap : 0);
        }
        freeSpace = mainSize - line->totalBaseSize;

        // Position along main axis
        float mainPos = 0;
        switch (container->justify) {
            case JUSTIFY_END: mainPos = freeSpace; break;
            case JUSTIFY_CENTER: mainPos = freeSpace / 2; break;
            case JUSTIFY_SPACE_BETWEEN:
                if (line->itemCount > 1) mainPos = 0;
                break;
            case JUSTIFY_SPACE_AROUND:
                mainPos = freeSpace / (line->itemCount * 2); break;
            case JUSTIFY_SPACE_EVENLY:
                mainPos = freeSpace / (line->itemCount + 1); break;
            default: break;
        }

        for (int i = 0; i < line->itemCount; i++) {
            int idx = isReverse ? line->itemCount - 1 - i : i;
            if (isRow) {
                line->items[idx].positionCoords.x = mainPos;
                if (container->justify == JUSTIFY_SPACE_BETWEEN && line->itemCount > 1) {
                    mainPos += line->items[idx].width + (freeSpace / (line->itemCount - 1));
                } else if (container->justify == JUSTIFY_SPACE_AROUND) {
                    mainPos += line->items[idx].width + (freeSpace / line->itemCount);
                } else if (container->justify == JUSTIFY_SPACE_EVENLY) {
                    mainPos += line->items[idx].width + (freeSpace / (line->itemCount + 1));
                } else {
                    mainPos += line->items[idx].width + container->gap;
                }
            } else {
                line->items[idx].positionCoords.y = mainPos;
                mainPos += line->items[idx].height + container->gap;
            }
        }

        // Cross axis positioning
        for (int i = 0; i < line->itemCount; i++) {
            AlignType align = (line->items[i].alignSelf == ALIGN_STRETCH) ? 
                            container->alignItems : line->items[i].alignSelf;
            float itemCrossPos = crossPos;
            switch (align) {
                case ALIGN_END: 
                    itemCrossPos = crossPos + (crossSize - line->items[i].height); break;
                case ALIGN_CENTER: 
                    itemCrossPos = crossPos + (crossSize - line->items[i].height) / 2; break;
                case ALIGN_STRETCH:
                    line->items[i].height = crossSize; break;
                default: break;
            }
            if (isRow) {
                line->items[i].positionCoords.y = itemCrossPos;
            } else {
                line->items[i].positionCoords.x = itemCrossPos;
            }
        }
        crossPos += line->height + (l < lineCount - 1 ? container->gap : 0);
    }

    // Copy back to original items
    for (int i = 0, k = 0; i < container->itemCount; i++) {
        if (container->items[i].position != POS_ABSOLUTE && 
            container->items[i].visibility != VIS_HIDDEN) {
            container->items[i] = layoutItems[k++];
        }
    }

    // Clean up
    free(layoutItems);
    for (int i = 0; i < lineCount; i++) {
        free(lines[i].items);
    }
    free(lines);
}

// Example usage (for manual testing)
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

    for (int i = 0; i < container.itemCount; i++) {
        printf("Item %d: x=%.1f, y=%.1f, w=%.1f, h=%.1f\n",
               i, container.items[i].positionCoords.x, container.items[i].positionCoords.y,
               container.items[i].width, container.items[i].height);
    }

    free(container.items);
    return 0;
}