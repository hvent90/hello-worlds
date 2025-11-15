#include "quadtree.h"
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

QuadTreeNode* QuadTreeNode_Create(Box3 bounds, Matrix localToWorld, Vector3 origin, float size, bool isRoot) {
    QuadTreeNode* node = (QuadTreeNode*)malloc(sizeof(QuadTreeNode));
    node->bounds = bounds;
    node->center = Box3_GetCenter(bounds);
    node->size = Box3_GetSize(bounds);
    node->childCount = 0;
    node->isRoot = isRoot;

    for (int i = 0; i < MAX_QUADTREE_CHILDREN; i++) {
        node->children[i] = NULL;
    }

    // Calculate sphere center
    node->sphereCenter = node->center;
    Vector3_ApplyMatrix(&node->sphereCenter, localToWorld);
    node->sphereCenter = Vector3Normalize(node->sphereCenter);
    node->sphereCenter = Vector3Scale(node->sphereCenter, size);
    node->sphereCenter = Vector3Add(node->sphereCenter, origin);

    return node;
}

void QuadTreeNode_Destroy(QuadTreeNode* node) {
    if (!node) return;

    for (int i = 0; i < node->childCount; i++) {
        QuadTreeNode_Destroy(node->children[i]);
    }

    free(node);
}

float QuadTreeNode_DistanceToPoint(QuadTreeNode* node, Vector3 pos) {
    return Vector3Distance(node->sphereCenter, pos);
}

void QuadTreeNode_CreateChildren(QuadTreeNode* node, Matrix localToWorld, Vector3 origin, float size) {
    Vector3 midpoint = node->center;

    // Create 4 child boxes (quadrants)
    Box3 boxes[4];

    // Bottom left
    boxes[0] = Box3_Create(node->bounds.min, midpoint);

    // Bottom right
    boxes[1] = Box3_Create(
        (Vector3){midpoint.x, node->bounds.min.y, 0},
        (Vector3){node->bounds.max.x, midpoint.y, 0}
    );

    // Top left
    boxes[2] = Box3_Create(
        (Vector3){node->bounds.min.x, midpoint.y, 0},
        (Vector3){midpoint.x, node->bounds.max.y, 0}
    );

    // Top right
    boxes[3] = Box3_Create(midpoint, node->bounds.max);

    for (int i = 0; i < 4; i++) {
        node->children[i] = QuadTreeNode_Create(boxes[i], localToWorld, origin, size, false);
    }
    node->childCount = 4;
}

void QuadTreeNode_Insert(QuadTreeNode* node, Vector3 pos, float minNodeSize, float comparatorValue,
                         Matrix localToWorld, Vector3 origin, float size) {
    float distToChild = QuadTreeNode_DistanceToPoint(node, pos);

    if (distToChild < node->size.x * comparatorValue && node->size.x > minNodeSize) {
        if (node->childCount == 0) {
            QuadTreeNode_CreateChildren(node, localToWorld, origin, size);
        }

        for (int i = 0; i < node->childCount; i++) {
            QuadTreeNode_Insert(node->children[i], pos, minNodeSize, comparatorValue, localToWorld, origin, size);
        }
    }
}

void QuadTreeNode_CollectLeaves(QuadTreeNode* node, QuadTreeNode*** nodes, int* count, int* capacity) {
    if (node->childCount == 0) {
        // This is a leaf node
        if (*count >= *capacity) {
            *capacity *= 2;
            *nodes = (QuadTreeNode**)realloc(*nodes, sizeof(QuadTreeNode*) * (*capacity));
        }
        (*nodes)[*count] = node;
        (*count)++;
        return;
    }

    for (int i = 0; i < node->childCount; i++) {
        QuadTreeNode_CollectLeaves(node->children[i], nodes, count, capacity);
    }
}

QuadTree* QuadTree_Create(Matrix localToWorld, float size, float minNodeSize, Vector3 origin, float comparatorValue) {
    QuadTree* tree = (QuadTree*)malloc(sizeof(QuadTree));
    tree->localToWorld = localToWorld;
    tree->size = size;
    tree->minNodeSize = minNodeSize;
    tree->origin = origin;
    tree->comparatorValue = comparatorValue;

    if (comparatorValue <= 0) {
        printf("ERROR: QuadTree comparison value must be greater than 0\n");
        free(tree);
        return NULL;
    }

    Box3 rootBounds = Box3_Create(
        (Vector3){-size, -size, 0},
        (Vector3){size, size, 0}
    );

    tree->root = QuadTreeNode_Create(rootBounds, localToWorld, origin, size, true);

    return tree;
}

void QuadTree_Insert(QuadTree* tree, Vector3 pos) {
    QuadTreeNode_Insert(tree->root, pos, tree->minNodeSize, tree->comparatorValue,
                       tree->localToWorld, tree->origin, tree->size);
}

void QuadTree_GetChildren(QuadTree* tree, QuadTreeNode*** outNodes, int* outCount) {
    int capacity = 16;
    *outNodes = (QuadTreeNode**)malloc(sizeof(QuadTreeNode*) * capacity);
    *outCount = 0;

    QuadTreeNode_CollectLeaves(tree->root, outNodes, outCount, &capacity);
}

void QuadTree_Destroy(QuadTree* tree) {
    if (!tree) return;
    QuadTreeNode_Destroy(tree->root);
    free(tree);
}
