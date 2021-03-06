#include "findUvOverlaps2.h"
#include "uvPoint.h"
#include "event.h"
#include "testCase.h"

#include <maya/MArgDatabase.h>
#include <maya/MArgList.h>
#include <maya/MGlobal.h>
#include <maya/MSelectionList.h>
#include <maya/MIntArray.h>
#include <maya/MString.h>
#include <maya/MStringArray.h>

#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <utility>
#include <deque>
#include <iterator>


FindUvOverlaps2::FindUvOverlaps2()
{
}

FindUvOverlaps2::~FindUvOverlaps2()
{
}

MSyntax FindUvOverlaps2::newSyntax()
{
    MSyntax syntax;
    syntax.addArg(MSyntax::kString);
    syntax.addFlag("-v", "-verbose", MSyntax::kBoolean);
    return syntax;
}


MStatus FindUvOverlaps2::doIt(const MArgList& args)
{
    MStatus status;

    MSelectionList sel;

    MArgDatabase argData(syntax(), args);

    status = argData.getCommandArgument(0, sel);
    if (status != MS::kSuccess) {
        MGlobal::displayError("You have to provide an object path");
        return MStatus::kFailure;
    }

    sel.getDagPath(0, mDagPath);
    mFnMesh.setObject(mDagPath);

    if (argData.isFlagSet("-verbose"))
        argData.getFlagArgument("-verbose", 0, verbose);
    else
        verbose = false;

    status = mDagPath.extendToShape();
    CHECK_MSTATUS_AND_RETURN_IT(status);

    if (mDagPath.apiType() != MFn::kMesh) {
        MGlobal::displayError("Selected object is not mesh.");
        return MStatus::kFailure;
    }

    if (verbose)
        MGlobal::displayInfo("Target object : " + mDagPath.fullPathName());

    return redoIt();
}

MStatus FindUvOverlaps2::redoIt()
{
    MStatus status;

    MIntArray uvShellIds;
    unsigned int nbUvShells;
    mFnMesh.getUvShellsIds(uvShellIds, nbUvShells);
    int numUVs = mFnMesh.numUVs();
    int numPolygons = mFnMesh.numPolygons();

    // Setup uv shell objects
    std::vector<UvShell> uvShellArray;
    uvShellArray.resize(nbUvShells);
    for (unsigned int i = 0; i < nbUvShells; i++) {
        UvShell shell;
        shell.shellIndex = i;
        uvShellArray[i] = shell;
    }

    // Get UV values and add them to the shell
    for (unsigned int uvId = 0; uvId < numUVs; uvId++) {
        float u, v;
        mFnMesh.getUV(uvId, u, v);
        UvShell& currentShell = uvShellArray[uvShellIds[uvId]];
        currentShell.uVector.push_back(u);
        currentShell.vVector.push_back(v);
    }

    // Setup bouding box inormation for each shell
    for (unsigned int id = 0; id < nbUvShells; id++) {
        UvShell& shell = uvShellArray[id];
        float uMax = *std::max_element(shell.uVector.begin(), shell.uVector.end());
        float vMax = *std::max_element(shell.vVector.begin(), shell.vVector.end());
        float uMin = *std::min_element(shell.uVector.begin(), shell.uVector.end());
        float vMin = *std::min_element(shell.vVector.begin(), shell.vVector.end());
        shell.uMax = uMax;
        shell.vMax = vMax;
        shell.uMin = uMin;
        shell.vMin = vMin;
    }

    // Loop all polygon faces and create edge objects
    for (unsigned int faceId = 0; faceId < numPolygons; faceId++) {
        int numPolygonVertices = mFnMesh.polygonVertexCount(faceId);
        for (int localVtx=0; localVtx<numPolygonVertices; localVtx++) {
            int curLocalIndex;
            int nextLocalIndex;
            if (localVtx == numPolygonVertices-1) {
                curLocalIndex = localVtx;
                nextLocalIndex = 0;
            }
            else {
                curLocalIndex = localVtx;
                nextLocalIndex = localVtx+1;
            }

            // UV indecis by local order
            int uvIdA;
            int uvIdB;
            mFnMesh.getPolygonUVid(faceId, curLocalIndex, uvIdA);
            mFnMesh.getPolygonUVid(faceId, nextLocalIndex, uvIdB);
            int currentShellIndex = uvShellIds[uvIdA];

            std::pair<int, int> edgeIndex;
            if (uvIdA < uvIdB)
                edgeIndex = std::make_pair(uvIdA, uvIdB);
            else
                edgeIndex = std::make_pair(uvIdB, uvIdA);

            // Get UV values and create edge objects
            float u_current, v_current;
            float u_next, v_next;
            mFnMesh.getPolygonUV(faceId, curLocalIndex, u_current, v_current);
            mFnMesh.getPolygonUV(faceId, nextLocalIndex, u_next, v_next);
            UvPoint p1(u_current, v_current, uvIdA, currentShellIndex);
            UvPoint p2(u_next, v_next, uvIdB, currentShellIndex);

            UvPoint beginPt;
            UvPoint endPt;

            if (p1 > p2) {
                beginPt = p2;
                endPt = p1;
            }
            else {
                beginPt = p1;
                endPt = p2;
            }

            // Create edge objects and insert them to shell edge set
            UvEdge edge(beginPt, endPt, edgeIndex);
            uvShellArray[currentShellIndex].edgeSet.insert(edge);
        }
    }

    // Countainer for a set of overlapped shell edges
    std::vector<std::set<UvEdge> > overlappedShells;

    // Countainer for a set of shell indices that doesn't have be checked as single shell
    std::set<int> dontCheck;

    int numShells = uvShellArray.size();

    // Get combinations of shell indices eg. (0, 1), (0, 2), (1, 2),,,
    std::vector<std::vector<int>> shellCombinations;
    makeCombinations(numShells, shellCombinations);

    for (int i=0; i<shellCombinations.size(); i++) {
        UvShell& shellA = uvShellArray[shellCombinations[i][0]];
        UvShell& shellB = uvShellArray[shellCombinations[i][1]];
        if (isShellOverlapped(shellA, shellB)) {
            // If two shells are overlapped, combine them into one single shell
            std::set<UvEdge> combinedEdges;
            combinedEdges.insert(shellA.edgeSet.begin(), shellA.edgeSet.end());
            combinedEdges.insert(shellB.edgeSet.begin(), shellB.edgeSet.end());
            overlappedShells.push_back(combinedEdges);

            dontCheck.insert(shellA.shellIndex);
            dontCheck.insert(shellB.shellIndex);
        }
    }

    // Countainer for a set of UV indices for the final result
    std::set<int> resultSet;

    // Run checker for overlapped shells
    for (int s=0; s<overlappedShells.size(); s++) {
        check(overlappedShells[s], resultSet);
    }

    // Run checker for single shells
    for (int n=0; n<uvShellArray.size(); n++) {
        if (std::find(dontCheck.begin(), dontCheck.end(), n) != dontCheck.end()) {
            // if contains, do nothing
        }
        else {
			check(uvShellArray[n].edgeSet, resultSet);
        }
    }

    // Setup return result
    MStringArray resultStrArray;
    for (std::set<int>::iterator fsi = resultSet.begin(); fsi != resultSet.end(); ++fsi) {
        MString index;
        index.set(*fsi);
        MString fullPath = mDagPath.fullPathName();
        MString n = fullPath + ".map[" + index + "]";
        resultStrArray.append(n);
    }
    MPxCommand::setResult(resultStrArray);

    return MS::kSuccess;
}


bool FindUvOverlaps2::isShellOverlapped(UvShell& shellA, UvShell& shellB)
{
    if (shellA.uMax < shellB.uMin)
        return false;

    if (shellA.uMin > shellB.uMax)
        return false;

    if (shellA.vMax < shellB.vMin)
        return false;

    if (shellA.vMin > shellB.vMax)
        return false;

    return true;
}


MStatus FindUvOverlaps2::check(std::set<UvEdge>& edges, std::set<int>& resultSet)
{
    std::set<UvEdge>::iterator iter;

    std::deque<Event> testQueue;

    int eventIndex = 0;
    for (iter = edges.begin(); iter != edges.end(); ++iter) {
        UvEdge edge = *iter;
        Event ev1("begin", edge.begin, edge, eventIndex);
        testQueue.push_back(ev1);
        eventIndex += 1;
        Event ev2("end", edge.end, edge, eventIndex);
        testQueue.push_back(ev2);
        eventIndex += 1;
    }

    std::sort(testQueue.begin(), testQueue.end());

    std::vector<UvEdge> status;
    status.reserve(edges.size());

    int numStatus;

    while(true) {
        if (testQueue.empty()) {
            break;
        }
        Event firstEvent = testQueue.front();
        UvEdge edge = firstEvent.edge;
        testQueue.pop_front();

        if (firstEvent.status == "begin") {
            status.push_back(edge);
            numStatus = status.size();

            if (numStatus == 1) {
                continue;
            }

            for (int i=0; i<numStatus; i++) {
                UvEdge& thisEdge = status[i];
                float& u1 = thisEdge.begin.u;
                float& u2 = thisEdge.end.u;
                for (int s=0; s<numStatus; s++) {
                    if (i == s)
                        continue;

                    UvEdge& targetEdge = status[s];

                    float u_small;
                    float u_big;
                    if (targetEdge.begin.u < targetEdge.end.u) {
                        u_small = targetEdge.begin.u;
                        u_big = targetEdge.end.u;
                    } else {
                        u_small = targetEdge.end.u;
                        u_big = targetEdge.begin.u;
                    }

                    if ((u_small < u1 && u1 < u_big) || (u_small < u2 && u2 < u_big)) {
                        if (thisEdge.isIntersected(targetEdge)) {
                            resultSet.insert(thisEdge.index.first);
                            resultSet.insert(thisEdge.index.second);
                            resultSet.insert(targetEdge.index.first);
                            resultSet.insert(targetEdge.index.second);
                        }
                    }
                    else {
                        // Do nothing
                    }
                }
            }
        }
        else if (firstEvent.status == "end") {
            auto found_itr = std::find(status.begin(), status.end(), edge);
            status.erase(found_itr);
        }
        else {
        }
    }

    return MS::kSuccess;
}


/* https://stackoverflow.com/questions/12991758/creating-all-possible-k-combinations-of-n-items-in-c */
void FindUvOverlaps2::makeCombinations(int N, std::vector<std::vector<int>>& vec)
{
    std::string bitmask(2, 1); // K leading 1's
    bitmask.resize(N, 0); // N-K trailing 0's

    // print integers and permute bitmask
    do {
        std::vector<int> sb;
        for (int i = 0; i < N; ++i) {
            if (bitmask[i]) {
                sb.push_back(i);
            }
        }
        vec.push_back(sb);
    } while (std::prev_permutation(bitmask.begin(), bitmask.end()));
}


MStatus FindUvOverlaps2::undoIt()
{
    return MS::kSuccess;
}

bool FindUvOverlaps2::isUndoable() const
{
    return false;
}

void* FindUvOverlaps2::creater()
{
    return new FindUvOverlaps2;
}
