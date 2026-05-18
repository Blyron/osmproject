#include "Pipeline/FOsmRawData.h"
#include "osm/OsmTypes.h"

FOsmRawData FOsmRawData::FromOsmData(const OsmData& Data)
{
	FOsmRawData Result;
	Result.MinLat = Data.minLat;
	Result.MaxLat = Data.maxLat;
	Result.MinLon = Data.minLon;
	Result.MaxLon = Data.maxLon;

	// Convert nodes
	Result.Nodes.Reserve(Data.nodes.size());
	for (const auto& [Id, Node] : Data.nodes)
	{
		FOsmNodeData& UENode = Result.Nodes.Add(Id);
		UENode.Id = Id;
		UENode.Lat = Node.lat;
		UENode.Lon = Node.lon;
		for (const auto& [Key, Value] : Node.tags)
		{
			UENode.Tags.Add(FString(UTF8_TO_TCHAR(Key.c_str())), FString(UTF8_TO_TCHAR(Value.c_str())));
		}
	}

	// Convert ways
	Result.Ways.Reserve(Data.ways.size());
	for (const auto& [Id, Way] : Data.ways)
	{
		FOsmWayData& UEWay = Result.Ways.Add(Id);
		UEWay.Id = Id;
		UEWay.NodeRefs.Reserve(Way.nodeRefs.size());
		for (OsmId Ref : Way.nodeRefs)
		{
			UEWay.NodeRefs.Add(Ref);
		}
		for (const auto& [Key, Value] : Way.tags)
		{
			UEWay.Tags.Add(FString(UTF8_TO_TCHAR(Key.c_str())), FString(UTF8_TO_TCHAR(Value.c_str())));
		}
	}

	// Convert relations
	Result.Relations.Reserve(Data.relations.size());
	for (const auto& [Id, Rel] : Data.relations)
	{
		FOsmRelationData& UERel = Result.Relations.Add(Id);
		UERel.Id = Id;
		UERel.Members.Reserve(Rel.members.size());
		for (const auto& Member : Rel.members)
		{
			FOsmRelationMember& UEMember = UERel.Members.AddDefaulted_GetRef();
			UEMember.Type = FString(UTF8_TO_TCHAR(Member.type.c_str()));
			UEMember.Ref = Member.ref;
			UEMember.Role = FString(UTF8_TO_TCHAR(Member.role.c_str()));
		}
		for (const auto& [Key, Value] : Rel.tags)
		{
			UERel.Tags.Add(FString(UTF8_TO_TCHAR(Key.c_str())), FString(UTF8_TO_TCHAR(Value.c_str())));
		}
	}

	return Result;
}

OsmData FOsmRawData::ToOsmData() const
{
	OsmData Data;
	Data.minLat = MinLat;
	Data.maxLat = MaxLat;
	Data.minLon = MinLon;
	Data.maxLon = MaxLon;

	for (const auto& [Id, UENode] : Nodes)
	{
		OsmNode& Node = Data.nodes[Id];
		Node.id = Id;
		Node.lat = UENode.Lat;
		Node.lon = UENode.Lon;
		for (const auto& [Key, Value] : UENode.Tags)
		{
			Node.tags[TCHAR_TO_UTF8(*Key)] = TCHAR_TO_UTF8(*Value);
		}
	}

	for (const auto& [Id, UEWay] : Ways)
	{
		OsmWay& Way = Data.ways[Id];
		Way.id = Id;
		Way.nodeRefs.reserve(UEWay.NodeRefs.Num());
		for (int64 Ref : UEWay.NodeRefs)
		{
			Way.nodeRefs.push_back(Ref);
		}
		for (const auto& [Key, Value] : UEWay.Tags)
		{
			Way.tags[TCHAR_TO_UTF8(*Key)] = TCHAR_TO_UTF8(*Value);
		}
	}

	for (const auto& [Id, UERel] : Relations)
	{
		OsmRelation& Rel = Data.relations[Id];
		Rel.id = Id;
		Rel.members.reserve(UERel.Members.Num());
		for (const auto& UEMember : UERel.Members)
		{
			OsmRelation::Member Member;
			Member.type = TCHAR_TO_UTF8(*UEMember.Type);
			Member.ref = UEMember.Ref;
			Member.role = TCHAR_TO_UTF8(*UEMember.Role);
			Rel.members.push_back(Member);
		}
		for (const auto& [Key, Value] : UERel.Tags)
		{
			Rel.tags[TCHAR_TO_UTF8(*Key)] = TCHAR_TO_UTF8(*Value);
		}
	}

	return Data;
}
