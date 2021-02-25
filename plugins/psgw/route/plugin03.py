import itertools

#
# Route function: Given the numerical Ids of nodes in partition A and B, the function
# returns a tuple (error, numeral of gateway)
def routeConnectionS(sizePartA, sizePartB, numGwd, numeralNodeA, numeralNodeB):
  A = [x for x in range(sizePartA)]
  B = [x for x in range(sizePartB)]
  P = list(itertools.product(A,B))
  numeralGw = P.index((numeralNodeA, numeralNodeB)) % numGwd

  return None, numeralGw

# Route function (extended interface): Make decision based on names of nodes to 
# take topology into account
# def routeConnectionX(nodeListPartA, nodeListPartB, gwList, nodeA, nodeB):
#	return Exception("Not implemented"), gwList[0]
routeConnectionX = None
