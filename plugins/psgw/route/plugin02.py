#
# Route function: Given the numerical Ids of nodes in partition A and B, the function
# returns a tuple (error, numeral of gateway)
def routeConnectionS(sizePartA, sizePartB, numGwd, numeralNodeA, numeralNodeB):
  numRelNodeA = numeralNodeA - numeralNodeA
  numRelNodeB = (numeralNodeB - numeralNodeA) % sizePartB
  numRelGw = (numRelNodeA + numRelNodeB) % numGwd
  numeralGw = (numeralNodeA + numRelGw) % numGwd

  return None, numeralGw

# Route function (extended interface): Make decision based on names of nodes to
# take topology into account
# def routeConnectionX(nodeListPartA, nodeListPartB, gwList, nodeA, nodeB):
#	return Exception("Not implemented"), gwList[0]
routeConnectionX = None
