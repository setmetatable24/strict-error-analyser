--!strict

local CHUNK_INDEX = 1
local CHUNK_SIZE = 90000

local HttpService = game:GetService("HttpService")

type InstanceNode = {
	name: string,
	className: string,
	source: string?,
	children: { InstanceNode }?,
}

type SortEntry = {
	index: number,
	inst: Instance,
}

type CachedExport = {
	encoded: string,
	totalLen: number,
	ranges: { { number } },
}

local TARGET_SERVICES = {
	"ReplicatedStorage",
	"ServerScriptService",
	"ServerStorage",
	"ReplicatedFirst",
	"StarterPlayer",
	"StarterGui",
	"StarterPack",
	"Workspace",
}

local function containsScript(root: Instance): boolean
	if root:IsA("LuaSourceContainer") then
		return true
	end
	for _, desc in root:GetDescendants() do
		if desc:IsA("LuaSourceContainer") then
			return true
		end
	end
	return false
end

local function isScriptClass(inst: Instance): boolean
	return inst.ClassName == "Script" or inst.ClassName == "LocalScript" or inst.ClassName == "ModuleScript"
end

local function serializeInstance(inst: Instance, isWorkspaceBranch: boolean, depth: number): InstanceNode?
	if isWorkspaceBranch then
		if inst:IsA("Terrain") or inst:IsA("Camera") then
			return nil
		end
		if depth >= 3 and not containsScript(inst) then
			return nil
		end
	end

	local node: InstanceNode = {
		name = inst.Name,
		className = inst.ClassName,
	}

	if isScriptClass(inst) then
		local container = inst :: LuaSourceContainer
		node.source = container.Source
	end

	local rawChildren = inst:GetChildren()
	local entries: { SortEntry } = table.create(#rawChildren)
	for i, child in rawChildren do
		entries[i] = { index = i, inst = child }
	end
	table.sort(entries, function(a: SortEntry, b: SortEntry): boolean
		if a.inst.Name ~= b.inst.Name then
			return a.inst.Name < b.inst.Name
		end
		if a.inst.ClassName ~= b.inst.ClassName then
			return a.inst.ClassName < b.inst.ClassName
		end
		return a.index < b.index
	end)

	local childrenList: { InstanceNode } = {}
	for _, entry in entries do
		local serialized = serializeInstance(entry.inst, isWorkspaceBranch, depth + 1)
		if serialized then
			table.insert(childrenList, serialized)
		end
	end

	if #childrenList > 0 then
		node.children = childrenList
	end

	return node
end

local function encodeNodeJson(node: InstanceNode, out: { string })
	table.insert(out, "{\"name\":")
	table.insert(out, HttpService:JSONEncode(node.name))
	table.insert(out, ",\"className\":")
	table.insert(out, HttpService:JSONEncode(node.className))
	if node.source ~= nil then
		table.insert(out, ",\"source\":")
		table.insert(out, HttpService:JSONEncode(node.source))
	end
	local kids = node.children
	if kids ~= nil and #kids > 0 then
		table.insert(out, ",\"children\":[")
		for idx, child in kids do
			if idx > 1 then
				table.insert(out, ",")
			end
			encodeNodeJson(child, out)
		end
		table.insert(out, "]")
	end
	table.insert(out, "}")
end

local function buildExportSnapshot(): CachedExport
	local serviceNodes: { InstanceNode } = {}
	for _, serviceName in TARGET_SERVICES do
		local ok, service = pcall(function(): Instance
			return game:GetService(serviceName)
		end)
		if ok and service then
			local serialized = serializeInstance(service, serviceName == "Workspace", 0)
			if serialized then
				table.insert(serviceNodes, serialized)
			end
		end
	end

	local rootTree: InstanceNode = {
		name = "DataModel",
		className = "DataModel",
		children = serviceNodes,
	}

	local outParts: { string } = {}
	encodeNodeJson(rootTree, outParts)
	local encoded = table.concat(outParts)
	local totalLen = #encoded

	local ranges: { { number } } = {}
	local pos = 1
	while pos <= totalLen do
		local endPos = math.min(totalLen, pos + CHUNK_SIZE - 1)
		while endPos < totalLen and endPos > pos do
			local nextByte = string.byte(encoded, endPos + 1)
			if bit32.band(nextByte, 0xC0) == 0x80 then
				endPos -= 1
			else
				break
			end
		end
		table.insert(ranges, { pos, endPos })
		pos = endPos + 1
	end

	return {
		encoded = encoded,
		totalLen = totalLen,
		ranges = ranges,
	}
end

local sharedBag = (shared :: any) :: { [string]: any }
local cached: CachedExport? = nil
if CHUNK_INDEX > 1 and typeof(sharedBag.__RBX_SA_CACHE) == "table" then
	cached = sharedBag.__RBX_SA_CACHE :: CachedExport
end

if not cached then
	local fresh = buildExportSnapshot()
	sharedBag.__RBX_SA_CACHE = fresh
	cached = fresh
end

local snapshot = cached :: CachedExport
local totalChunks = math.max(1, #snapshot.ranges)
if CHUNK_INDEX < 1 or CHUNK_INDEX > totalChunks then
	error(string.format("Invalid CHUNK_INDEX %d (totalChunks=%d)", CHUNK_INDEX, totalChunks))
end

local slice = if #snapshot.ranges > 0
	then string.sub(snapshot.encoded, snapshot.ranges[CHUNK_INDEX][1], snapshot.ranges[CHUNK_INDEX][2])
	else "{}"

if CHUNK_INDEX == totalChunks and totalChunks > 1 then
	sharedBag.__RBX_SA_CACHE = nil
end

return string.format("RBX_SA_CHUNK %d/%d %d\n%s", CHUNK_INDEX, totalChunks, snapshot.totalLen, slice)
