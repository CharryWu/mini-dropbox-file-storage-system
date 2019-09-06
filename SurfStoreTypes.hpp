#ifndef SURFSTORETYPES_HPP
#define SURFSTORETYPES_HPP

#include <tuple>
#include <map>
#include <list>
#include <string>

typedef tuple<int, list<string>> FileInfo; // tuple(version:int, hashlist:list<string>
typedef map<string, FileInfo> FileInfoMap; // filename:string -> tuple(version:int, hashlist:list<string>)
typedef map<string, string> HashDataMap; // hash: string -> data_block: string


const string RAND = "random";
const string TWO_RAND = "tworandom";
const string LOCAL = "local";
const string LOCAL_CLOSE = "localclosest";
const string LOCAL_FAR = "localfarthest";

#endif // SURFSTORETYPES_HPP
